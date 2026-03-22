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

// Server callbacks
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
uint32_t lastRedBlink=0, lastYellowBlink=0;
bool redState=false, yellowState=false;
const int BLINK_BATTERY=500;
const int BLINK_BATTERY_FAST=250;
const int BLINK_BLE=500;

// ================= SD =================
#define MAX_FILE_SIZE 30*1024
bool SD_OK=false;
File csvFile, txtFile;
String currentCSVFile, currentTXTFile;

// ================= FUNCTION DECLARATIONS =================
void setLED(bool r, bool g, bool b);
void writeRegister(uint8_t reg, uint8_t data);
void readBytes(uint8_t reg, uint8_t count, uint8_t* dest);
void readIMU(IMUData &data);
void readMagnetometer(IMUData &data);
void calculateOrientation(const IMUData &imu, float &roll, float &pitch, float &yaw);
float readLoadCell();
float readBattery();
int batteryPercent(float vbat);
String getTimestamp();
void sdInit();
String createDailyFolder();
void openNewCSV();
void openNewTXT();
void logCSV(String line);
void logTXT(String line);
void blinkLEDs(int pct, bool bleConnected);
void wifiSetup();
void bleSetup();
void sendBLE(String data);
void streamWiFi(String data);

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(2000);

  // LED
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setLED(false,false,false);

  // SPI & IMU
  SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);
  pinMode(PIN_CS_IMU, OUTPUT);
  digitalWrite(PIN_CS_IMU,HIGH);

  // Wake MPU9250
  writeRegister(0x6B,0x00); // PWR_MGMT_1=0
  delay(100);

  // Setup AK8963 (magnetometer)
  writeRegister(0x37, 0x02); // INT_PIN_CFG: enable bypass
  delay(10);
  uint8_t asa[3];
  readBytes(AK8963_ASAX,3,asa);
  for(int i=0;i<3;i++) magAdjust[i] = ((float)(asa[i]-128)/256.0)+1.0;
  writeRegister(AK8963_CNTL1,0x16); // 16-bit, 100Hz
  delay(10);

  // HX711
  scale.begin(PIN_HX_DT,PIN_HX_SCK);
  scale.set_scale(16338);
  scale.tare();

  // SD
  sdInit();

  // WiFi & NTP
  wifiSetup();
  timeClient.begin();
  tcpServer.begin();

  // BLE
  bleSetup();

  // Open first files
  openNewCSV();
  openNewTXT();
}

// ================= LOOP =================
void loop() {
  uint32_t loopStart = millis();
  timeClient.update();

  // ==== IMU ====
  readIMU(imu);
  readMagnetometer(imu);
  float roll,pitch,yaw;
  calculateOrientation(imu, roll, pitch, yaw);

  // ==== Load Cell ====
  float weight = readLoadCell();

  // ==== Battery ====
  float vbat = readBattery();
  int pct = batteryPercent(vbat);

  // ==== Timestamp ====
  String ts = getTimestamp();

  // ==== LED ====
  blinkLEDs(pct, deviceConnected);

  // ==== CSV Logging ====
  String csvLine = ts + ";" + String(weight,2) + ";" + String(roll,2) + ";" + String(pitch,2) + ";" + String(yaw,2) + ";"
                   + String(imu.ax/16384.0,3) + ";" + String(imu.ay/16384.0,3) + ";" + String(imu.az/16384.0,3) + ";"
                   + String(imu.gx/131.0,3) + ";" + String(imu.gy/131.0,3) + ";" + String(imu.gz/131.0,3) + ";"
                   + String(vbat,2) + ";" + String(pct);
  logCSV(csvLine);

  // ==== TXT Logging ====
  String txtLine = "Timestamp: "+ts+"\nWeight: "+String(weight,2)+" kg\nOrientation: Roll="+String(roll,2)+", Pitch="+String(pitch,2)+", Yaw="+String(yaw,2)+
                   "\nAcceleration: X="+String(imu.ax/16384.0,3)+", Y="+String(imu.ay/16384.0,3)+", Z="+String(imu.az/16384.0,3)+
                   "\nGyroscope: X="+String(imu.gx/131.0,3)+", Y="+String(imu.gy/131.0,3)+", Z="+String(imu.gz/131.0,3)+
                   "\nMagnetometer: X="+String(imu.mx*magAdjust[0],3)+", Y="+String(imu.my*magAdjust[1],3)+", Z="+String(imu.mz*magAdjust[2],3)+
                   "\nBattery: "+String(vbat,2)+"V ("+String(pct)+"%)\n----------------------------------------";
  logTXT(txtLine);

  // ==== Serial, BLE & WiFi Stream ====
  Serial.println(csvLine);
  sendBLE(csvLine);
  streamWiFi(csvLine);

  // ==== Maintain ~50ms loop ====
  uint32_t elapsed = millis()-loopStart;
  if(elapsed<50) delay(50-elapsed);
}

// ================= FUNCTIONS =================
void setLED(bool r, bool g, bool b){ 
  digitalWrite(PIN_LED_R,r?HIGH:LOW); 
  digitalWrite(PIN_LED_G,g?HIGH:LOW); 
  digitalWrite(PIN_LED_B,b?HIGH:LOW);
}

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

void readIMU(IMUData &data){ 
  uint8_t buf[14]; 
  readBytes(ACCEL_XOUT_H,14,buf); 
  data.ax=(buf[0]<<8)|buf[1]; 
  data.ay=(buf[2]<<8)|buf[3]; 
  data.az=(buf[4]<<8)|buf[5]; 
  data.gx=(buf[8]<<8)|buf[9]; 
  data.gy=(buf[10]<<8)|buf[11]; 
  data.gz=(buf[12]<<8)|buf[13];
}

void readMagnetometer(IMUData &data){ 
  uint8_t buf[7]; 
  SPIbus.beginTransaction(IMU_SPI_SETTINGS); 
  digitalWrite(PIN_CS_IMU,LOW); 
  SPIbus.transfer(0x03|0x80); 
  for(int i=0;i<7;i++) buf[i]=SPIbus.transfer(0x00); 
  digitalWrite(PIN_CS_IMU,HIGH); 
  SPIbus.endTransaction(); 
  data.mx=(buf[1]<<8)|buf[0]; 
  data.my=(buf[3]<<8)|buf[2]; 
  data.mz=(buf[5]<<8)|buf[4];
}

void calculateOrientation(const IMUData &imu,float &roll,float &pitch,float &yaw){ 
  float ax=imu.ax/16384.0, ay=imu.ay/16384.0, az=imu.az/16384.0; 
  float mx=imu.mx*magAdjust[0], my=imu.my*magAdjust[1], mz=imu.mz*magAdjust[2]; 
  roll=atan2(ay,az)*180.0/PI; 
  pitch=atan2(-ax,sqrt(ay*ay+az*az))*180.0/PI; 
  float mx2=mx*cos(pitch)+mz*sin(pitch); 
  float my2=mx*sin(roll)*sin(pitch)+my*cos(roll)-mz*sin(roll)*cos(pitch); 
  yaw=atan2(-my2,mx2)*180.0/PI;
}

float readLoadCell(){ return scale.is_ready()?scale.get_units(1):NAN; }
float readBattery(){ return analogRead(PIN_BAT_ADC)/4095.0*3.3*2*1.023; }
int batteryPercent(float vbat){ return max(0,min(100,(int)((vbat-3.2)/(4.2-3.2)*100))); }

void sdInit(){ 
  SD_OK=SD.begin(PIN_CS_SD,SPIbus); 
  if(!SD_OK) Serial.println("SD failed"); 
}

String createDailyFolder(){ 
  String folder = "/LOGS";
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.update();
    time_t rawTime = timeClient.getEpochTime();
    struct tm *timeinfo = localtime(&rawTime);
    char buf[16];
    sprintf(buf,"/LOGS/%04d-%02d-%02d",timeinfo->tm_year+1900,timeinfo->tm_mon+1,timeinfo->tm_mday);
    folder = String(buf);
  }
  if(!SD.exists(folder)) {
    if(!SD.mkdir(folder)) Serial.println("Failed to create folder: "+folder);
  }
  return folder;
}

void openNewCSV(){ 
  String folder=createDailyFolder(); 
  currentCSVFile=folder+"/dados.csv"; 
  if(csvFile) csvFile.close(); 
  csvFile=SD.open(currentCSVFile,FILE_WRITE); 
  if(csvFile) csvFile.println("Timestamp;Peso;Roll;Pitch;Yaw;AX;AY;AZ;GX;GY;GZ;VBAT;BAT%");
  else Serial.println("Failed to open CSV file!");
}

void openNewTXT(){ 
  String folder=createDailyFolder(); 
  currentTXTFile=folder+"/log.txt"; 
  if(txtFile) txtFile.close(); 
  txtFile=SD.open(currentTXTFile,FILE_WRITE); 
  if(!txtFile) Serial.println("Failed to open TXT file!");
}

void logCSV(String line){ 
  if(!SD_OK) return; 
  if(csvFile){ 
    csvFile.println(line); 
    csvFile.flush(); 
    if(csvFile.size()>MAX_FILE_SIZE) openNewCSV(); 
  } 
}

void logTXT(String line){ 
  if(!SD_OK) return; 
  if(txtFile){ 
    txtFile.println(line); 
    txtFile.flush(); 
    if(txtFile.size()>MAX_FILE_SIZE) openNewTXT(); 
  } 
}

String getTimestamp(){ 
  if(WiFi.status()==WL_CONNECTED){ 
    timeClient.update();
    return timeClient.getFormattedTime();
  } 
  return "00:00:00"; 
}

void blinkLEDs(int pct, bool bleConnected){
  uint32_t now=millis();
  if(pct<20){
    int blink=pct<5?BLINK_BATTERY_FAST:BLINK_BATTERY;
    if(now-lastRedBlink>=blink){ redState^=1; lastRedBlink=now; }
    setLED(redState,false,false);
  } else {
    digitalWrite(PIN_LED_R,LOW);
    if(bleConnected){
      if(now-lastYellowBlink>=BLINK_BLE){ yellowState^=1; lastYellowBlink=now; }
      digitalWrite(PIN_LED_B,yellowState?HIGH:LOW);
    } else digitalWrite(PIN_LED_B,LOW);
    analogWrite(PIN_LED_G,bleConnected?255:120);
  }
}

void wifiSetup(){ 
  WiFi.begin(WIFI_SSID,WIFI_PASS); 
  uint32_t start=millis(); 
  while(WiFi.status()!=WL_CONNECTED && millis()-start<10000) delay(500); 
  if(WiFi.status()==WL_CONNECTED) Serial.println("WiFi connected"); 
}

// ===== BLE SETUP =====
void bleSetup() {
  BLEDevice::init("ESP32-Time");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Notify + Read characteristic
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );

  BLE2902* p2902 = new BLE2902();
  pCharacteristic->addDescriptor(p2902);

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("BLE started, advertising...");
}

// ======== BLE SEND WITH CHUNKS ========
void sendBLE(String data){
  if (!deviceConnected || !pCharacteristic) return;

  const int CHUNK_SIZE = 100;
  int len = data.length();
  int pos = 0;

  while (pos < len) {
    int size = min(CHUNK_SIZE, len - pos);
    String chunkStr = data.substring(pos, pos + size);
    const uint8_t* chunk = (const uint8_t*)chunkStr.c_str();
    pCharacteristic->setValue(chunk, size);
    pCharacteristic->notify();
    pos += size;
    delay(5);
  }

  const uint8_t nl = '\n';
  pCharacteristic->setValue(&nl, 1);
  pCharacteristic->notify();
}

void streamWiFi(String data){ 
  WiFiClient client=tcpServer.available(); 
  if(client){ 
    client.println(data); 
    client.flush(); 
  } 
}
