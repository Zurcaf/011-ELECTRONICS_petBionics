#include <SPI.h>
#include <SD.h>
#include <HX711.h>
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <sys/time.h>
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
bool timeSynced = false;

// ================= BLE =================
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("BLE device connected");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    pServer->getAdvertising()->start();
  }
};

// ================= HX711 =================
HX711 scale;

// ================= IMU =================
SPIClass SPIbus(FSPI);
const SPISettings IMU_SPI_SETTINGS(2000000, MSBFIRST, SPI_MODE3);
#define ACCEL_XOUT_H 0x3B

struct IMUData { int16_t ax, ay, az, gx, gy, gz; };
IMUData imu;
float yawAngle = 0;

// ================= SD =================
bool SD_OK = false;
File csvFile;
String currentFolder = "";

// ================= LED =================
void setLED(bool r,bool g,bool b){
  digitalWrite(PIN_LED_R,r);
  digitalWrite(PIN_LED_G,g);
  digitalWrite(PIN_LED_B,b);
}

uint32_t ledLastToggle = 0;
bool redState = false;

void updateLEDs(float batteryPct) {

  bool green = timeSynced;
  bool yellow = deviceConnected;
  bool red = false;
  uint32_t now = millis();

  if (batteryPct < 10) {
    if (now - ledLastToggle >= 200) {
      redState = !redState;
      ledLastToggle = now;
    }
    red = redState;
  }
  else if (batteryPct < 15) {
    if (now - ledLastToggle >= 500) {
      redState = !redState;
      ledLastToggle = now;
    }
    red = redState;
  }
  else {
    red = false;
    redState = false;
  }

  setLED(red, green, yellow);
}

// ================= WIFI + NTP =================
void wifiSetup(){

  timeSynced = false;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");

  uint32_t start = millis();
  while(WiFi.status() != WL_CONNECTED && millis()-start < 15000){
    delay(500);
    Serial.print(".");
  }

  if(WiFi.status() == WL_CONNECTED){

    Serial.println("\nWiFi connected");
    configTime(0, 0, "pool.ntp.org");

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) {

      Serial.println("Time synchronized");

      timeSynced = true;
      updateLEDs(100);
      delay(2000);

      timeSynced = false;
      updateLEDs(100);
    }

    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi disabled");
  }
}

// ================= SD =================
void sdInit(){
  SD_OK = SD.begin(PIN_CS_SD, SPIbus);
  if(!SD_OK){ Serial.println("SD FAILED"); return; }
  Serial.println("SD OK");
}

// ================= FILE =================
String createDailyFolder(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "/LOGS/NO_TIME";

  char path[32];
  sprintf(path,"/LOGS/%04d-%02d-%02d",
          timeinfo.tm_year+1900,
          timeinfo.tm_mon+1,
          timeinfo.tm_mday);

  if(!SD.exists("/LOGS")) SD.mkdir("/LOGS");
  if(!SD.exists(path)) SD.mkdir(path);

  return String(path);
}

String generateTimeFile(String ext){
  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm timeinfo;
  localtime_r(&tv.tv_sec, &timeinfo);

  char fileName[64];
  sprintf(fileName,"%s/%02d-%02d-%02d.%03ld.%s",
          currentFolder.c_str(),
          timeinfo.tm_hour,
          timeinfo.tm_min,
          timeinfo.tm_sec,
          tv.tv_usec/1000,
          ext.c_str());

  return String(fileName);
}

void openNewCSV(){
  if(!SD_OK) return;
  currentFolder = createDailyFolder();
  String fileName = generateTimeFile("csv");

  if(csvFile) csvFile.close();
  csvFile = SD.open(fileName, FILE_WRITE);
  if(!csvFile){ Serial.println("CSV open failed"); return; }

  csvFile.println("Timestamp;Peso;Roll;Pitch;Yaw;AX;AY;AZ;GX;GY;GZ;VBAT;BAT%");
  csvFile.flush();
}

// ================= IMU =================
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

void calculateOrientation(const IMUData &imu,float &roll,float &pitch,float &yaw,float dt){
  float ax=imu.ax/16384.0;
  float ay=imu.ay/16384.0;
  float az=imu.az/16384.0;
  float gz=imu.gz/131.0;

  roll = atan2(ay,az)*180.0/PI;
  pitch = atan2(-ax, sqrt(ay*ay+az*az))*180.0/PI;

  yawAngle += gz*dt;
  if(yawAngle>180) yawAngle-=360;
  if(yawAngle<-180) yawAngle+=360;
  yaw = yawAngle;
}

// ================= BLE =================
void bleSetup(){
  BLEDevice::init("ESP32-LOGGER");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );

  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  pServer->getAdvertising()->start();
}

void sendBLE(String data){
  if(!deviceConnected) return;

  const int chunkSize = 20;
  for(int i=0;i<data.length();i+=chunkSize){
    pCharacteristic->setValue(data.substring(i,i+chunkSize).c_str());
    pCharacteristic->notify();
    delay(2);
  }
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

  scale.begin(PIN_HX_DT,PIN_HX_SCK);
  scale.set_scale(16338);
  scale.tare();

  wifiSetup();
  sdInit();
  openNewCSV();
  bleSetup();
}

// ================= LOOP =================
const uint32_t LOOP_PERIOD_MS = 50;
String sdBuffer="";
uint32_t lastFlush=0;

void loop(){

  static uint32_t lastLoopTime=0;
  uint32_t now=millis();

  if(now-lastLoopTime>=LOOP_PERIOD_MS){

    lastLoopTime+=LOOP_PERIOD_MS;
    float dt=LOOP_PERIOD_MS/1000.0;

    readIMU(imu);

    float roll,pitch,yaw;
    calculateOrientation(imu,roll,pitch,yaw,dt);

    float weight=scale.is_ready()?scale.get_units(1):NAN;
    float vbat=analogRead(PIN_BAT_ADC)/4095.0*3.3*2*0.943;
    int pct=max(0,min(100,(int)((vbat-3.2)/(4.2-3.2)*100)));

    updateLEDs(pct);

    struct timeval tv;
    gettimeofday(&tv,NULL);

    struct tm timeinfo;
    localtime_r(&tv.tv_sec,&timeinfo);

    char timestamp[24];
    sprintf(timestamp,"%02d:%02d:%02d.%03ld",
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec,
            tv.tv_usec/1000);

    String csvLine =
      String(timestamp)+";"+
      String(weight,2)+";"+
      String(roll,2)+";"+
      String(pitch,2)+";"+
      String(yaw,2)+";"+
      String(imu.ax)+";"+
      String(imu.ay)+";"+
      String(imu.az)+";"+
      String(imu.gx)+";"+
      String(imu.gy)+";"+
      String(imu.gz)+";"+
      String(vbat,2)+";"+
      String(pct);

    sdBuffer+=csvLine+"\n";

    if(sdBuffer.length()>200 || millis()-lastFlush>1000){
      if(SD_OK && csvFile){
        csvFile.print(sdBuffer);
        csvFile.flush();
      }
      sdBuffer="";
      lastFlush=millis();
    }

    sendBLE(csvLine);
    Serial.println(csvLine);
  }
}