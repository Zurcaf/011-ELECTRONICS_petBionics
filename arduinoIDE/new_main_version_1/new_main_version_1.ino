#include <SPI.h>
#include <SD.h>
#include "HX711.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>

// ================= PINOUT =================
#define PIN_SPI_SCK D6
#define PIN_SPI_MISO D5
#define PIN_SPI_MOSI D4
#define PIN_CS_IMU D7
#define PIN_CS_SD D8

#define PIN_HX_DT D10
#define PIN_HX_SCK D9

#define PIN_LED_R D1
#define PIN_LED_G D2
#define PIN_LED_B D3

#define PIN_BAT_ADC A0

// ================= WIFI =================
const char* WIFI_SSID = "AndroidAP";
const char* WIFI_PASS = "qahc8316";
WiFiServer tcpServer(5000);

// ================= NTP =================
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// ================= BLE =================
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer){ deviceConnected = true; }
  void onDisconnect(BLEServer* pServer){ deviceConnected = false; }
};

// ================= HX711 =================
HX711 scale;

// ================= IMU =================
SPIClass SPIbus(FSPI);
const SPISettings IMU_SPI_SETTINGS(2000000, MSBFIRST, SPI_MODE3);

#define ACCEL_XOUT_H 0x3B
#define AK8963_CNTL1 0x0A
#define AK8963_ASAX 0x10

struct IMUData {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  int16_t mx, my, mz;
};

IMUData imu;
float magAdjust[3] = {1.0,1.0,1.0};

// ================= LED =================
uint32_t lastRedBlink=0,lastYellowBlink=0;
bool redState=false,yellowState=false;
const int BLINK_BATTERY=500;
const int BLINK_BATTERY_FAST=250;
const int BLINK_BLE=500;

// ================= SD =================
#define MAX_FILE_SIZE (30*1024)
bool SD_OK=false;
File csvFile, txtFile;
String currentCSVFile,currentTXTFile;
String currentFolder="";

// ================= FUNCTIONS =================
void setLED(bool r,bool g,bool b){
  digitalWrite(PIN_LED_R,r);
  digitalWrite(PIN_LED_G,g);
  digitalWrite(PIN_LED_B,b);
}

// ================= IMU =================
void writeRegister(uint8_t reg,uint8_t data){
  SPIbus.beginTransaction(IMU_SPI_SETTINGS);
  digitalWrite(PIN_CS_IMU,LOW);
  SPIbus.transfer(reg);
  SPIbus.transfer(data);
  digitalWrite(PIN_CS_IMU,HIGH);
  SPIbus.endTransaction();
}

void readBytes(uint8_t reg,uint8_t count,uint8_t* dest){
  SPIbus.beginTransaction(IMU_SPI_SETTINGS);
  digitalWrite(PIN_CS_IMU,LOW);
  SPIbus.transfer(reg|0x80);
  for(int i=0;i<count;i++) dest[i]=SPIbus.transfer(0x00);
  digitalWrite(PIN_CS_IMU,HIGH);
  SPIbus.endTransaction();
}

void readIMU(IMUData &d){
  uint8_t buf[14];
  readBytes(ACCEL_XOUT_H,14,buf);
  d.ax=(buf[0]<<8)|buf[1];
  d.ay=(buf[2]<<8)|buf[3];
  d.az=(buf[4]<<8)|buf[5];
  d.gx=(buf[8]<<8)|buf[9];
  d.gy=(buf[10]<<8)|buf[11];
  d.gz=(buf[12]<<8)|buf[13];
}

void calculateOrientation(const IMUData &imu,float &roll,float &pitch,float &yaw){
  float ax=imu.ax/16384.0, ay=imu.ay/16384.0, az=imu.az/16384.0;
  roll=atan2(ay,az)*180.0/PI;
  pitch=atan2(-ax,sqrt(ay*ay+az*az))*180.0/PI;
  yaw=0;
}

// ================= SENSORS =================
float readLoadCell(){ return scale.is_ready()?scale.get_units(1):NAN; }
float readBattery(){ return analogRead(PIN_BAT_ADC)/4095.0*3.3*2*1.023; }
int batteryPercent(float vbat){ return max(0,min(100,(int)((vbat-3.2)/(4.2-3.2)*100))); }

// ================= SD DATE FOLDER =================
String createDailyFolder(){
  if(!SD_OK) return "/";

  if(WiFi.status()!=WL_CONNECTED) return "/LOGS";

  timeClient.update();
  time_t raw=timeClient.getEpochTime();
  struct tm *t=localtime(&raw);

  char buf[32];
  sprintf(buf,"/LOGS/%04d-%02d-%02d",
          t->tm_year+1900,t->tm_mon+1,t->tm_mday);

  if(!SD.exists("/LOGS")) SD.mkdir("/LOGS");
  if(!SD.exists(buf)) SD.mkdir(buf);

  return String(buf);
}

// ================= FILE OPEN =================
String generateTimeFileName(String ext){
  timeClient.update();
  time_t raw=timeClient.getEpochTime();
  struct tm *t=localtime(&raw);

  char buf[50];
  sprintf(buf,"%s/%02d-%02d-%02d.%s",
          currentFolder.c_str(),
          t->tm_hour,t->tm_min,t->tm_sec,
          ext.c_str());
  return String(buf);
}

void openNewCSV(){
  if(!SD_OK) return;
  currentFolder=createDailyFolder();
  currentCSVFile=generateTimeFileName("csv");

  if(csvFile) csvFile.close();
  csvFile=SD.open(currentCSVFile,FILE_WRITE);

  if(csvFile){
    csvFile.println("Timestamp;Peso;Roll;Pitch;Yaw;AX;AY;AZ;GX;GY;GZ;VBAT;BAT%");
    csvFile.flush();
  }
}

void openNewTXT(){
  if(!SD_OK) return;
  currentFolder=createDailyFolder();
  currentTXTFile=generateTimeFileName("txt");

  if(txtFile) txtFile.close();
  txtFile=SD.open(currentTXTFile,FILE_WRITE);
}

// ================= LOGGING =================
void logCSV(String line){
  if(!SD_OK||!csvFile) return;
  csvFile.println(line);
  csvFile.flush();
  if(csvFile.size()>MAX_FILE_SIZE) openNewCSV();
}

void logTXT(String line){
  if(!SD_OK||!txtFile) return;
  txtFile.println(line);
  txtFile.flush();
  if(txtFile.size()>MAX_FILE_SIZE) openNewTXT();
}

// ================= BLE =================
void sendBLE(String data){
  if(!deviceConnected||!pCharacteristic) return;
  pCharacteristic->setValue(data.c_str());
  pCharacteristic->notify();
}

void bleSetup(){
  BLEDevice::init("ESP32-Time");
  pServer=BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService=pServer->createService(SERVICE_UUID);
  pCharacteristic=pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  pServer->getAdvertising()->start();
}

// ================= WIFI =================
void wifiSetup(){
  WiFi.begin(WIFI_SSID,WIFI_PASS);
  uint32_t start=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-start<10000) delay(500);
}

void streamWiFi(String data){
  WiFiClient client=tcpServer.available();
  if(client) client.println(data);
}

// ================= SETUP =================
void setup(){
  Serial.begin(115200);
  delay(2000);

  pinMode(PIN_LED_R,OUTPUT);
  pinMode(PIN_LED_G,OUTPUT);
  pinMode(PIN_LED_B,OUTPUT);

  SPIbus.begin(PIN_SPI_SCK,PIN_SPI_MISO,PIN_SPI_MOSI,PIN_CS_IMU);
  pinMode(PIN_CS_IMU,OUTPUT);
  digitalWrite(PIN_CS_IMU,HIGH);

  writeRegister(0x6B,0x00);
  delay(100);

  scale.begin(PIN_HX_DT,PIN_HX_SCK);
  scale.set_scale(16338);
  scale.tare();

  SD_OK=SD.begin(PIN_CS_SD,SPIbus);

  wifiSetup();
  timeClient.begin();
  tcpServer.begin();
  bleSetup();

  openNewCSV();
  openNewTXT();
}

// ================= LOOP =================
void loop(){
  uint32_t start=millis();
  timeClient.update();

  readIMU(imu);
  float roll,pitch,yaw;
  calculateOrientation(imu,roll,pitch,yaw);

  float weight=readLoadCell();
  float vbat=readBattery();
  int pct=batteryPercent(vbat);

  String ts=timeClient.getFormattedTime();

  String csvLine=ts+";"+String(weight,2)+";"+String(roll,2)+";"+String(pitch,2)+";"+String(yaw,2)+";"+
                 String(imu.ax/16384.0,3)+";"+String(imu.ay/16384.0,3)+";"+String(imu.az/16384.0,3)+";"+
                 String(imu.gx/131.0,3)+";"+String(imu.gy/131.0,3)+";"+String(imu.gz/131.0,3)+";"+
                 String(vbat,2)+";"+String(pct);

  logCSV(csvLine);
  logTXT(csvLine);

  Serial.println(csvLine);
  sendBLE(csvLine);
  streamWiFi(csvLine);

  uint32_t elapsed=millis()-start;
  if(elapsed<50) delay(50-elapsed);
}
