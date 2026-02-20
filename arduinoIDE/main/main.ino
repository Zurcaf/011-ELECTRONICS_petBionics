#include <SPI.h>
#include <SD.h>
#include <HX711.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
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
const char* WIFI_SSID = "composites";
const char* WIFI_PASS = "Composites2019";
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

// BLE callbacks
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("BLE device connected");
  }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("BLE device disconnected, restarting advertising");
    pServer->getAdvertising()->start(); // auto restart
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
#define MAX_FILE_SIZE (30*1024)
bool SD_OK = false;
File csvFile, txtFile;
String currentFolder = "";
String currentCSVFile = "";
String currentTXTFile = "";

// ================= UTIL =================
void setLED(bool r,bool g,bool b){
  digitalWrite(PIN_LED_R,r);
  digitalWrite(PIN_LED_G,g);
  digitalWrite(PIN_LED_B,b);
}

// ================= LED STATUS =================
uint32_t ledLastToggle = 0;
bool redState = false;

void updateLEDs(float batteryPct) {
  bool green = (WiFi.status() == WL_CONNECTED);
  bool yellow = deviceConnected;
  bool red = false;
  uint32_t now = millis();

  if (batteryPct < 10) {
    if (now - ledLastToggle >= 200) { redState = !redState; ledLastToggle = now; }
    red = redState;
  } else if (batteryPct < 15) {
    if (now - ledLastToggle >= 500) { redState = !redState; ledLastToggle = now; }
    red = redState;
  } else {
    red = false; redState = false;
  }

  setLED(red, green, yellow);
}

// ================= WIFI + NTP =================
void wifiSetup(){
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");

  uint32_t start = millis();
  while(WiFi.status() != WL_CONNECTED && millis()-start < 15000){
    delay(500); Serial.print(".");
  }

  if(WiFi.status() == WL_CONNECTED){
    Serial.println("\nWiFi connected");

    timeClient.begin();
    while(!timeClient.update()){ timeClient.forceUpdate(); }

    time_t epoch = timeClient.getEpochTime();
    struct timeval tv; tv.tv_sec = epoch; tv.tv_usec = 0;
    settimeofday(&tv, NULL);

    Serial.println("System time synchronized");
  } else {
    Serial.println("\nWiFi failed (timestamps may be wrong)");
  }
}

// ================= SD =================
void sdInit(){
  SD_OK = SD.begin(PIN_CS_SD, SPIbus);
  if(!SD_OK){ Serial.println("SD initialization FAILED"); return; }
  Serial.println("SD initialized successfully");
}

// ================= DATE/TIME FILE MANAGEMENT =================
String createDailyFolder(){
  time_t now; time(&now);
  struct tm *t = localtime(&now);
  char path[32];
  sprintf(path,"/LOGS/%04d-%02d-%02d", t->tm_year+1900, t->tm_mon+1, t->tm_mday);
  if(!SD.exists("/LOGS")) SD.mkdir("/LOGS");
  if(!SD.exists(path)) SD.mkdir(path);
  return String(path);
}

String generateTimeFile(String ext){
  time_t now; time(&now);
  struct tm *t = localtime(&now);
  char fileName[50];
  sprintf(fileName,"%s/%02d-%02d-%02d.%s",
          currentFolder.c_str(), t->tm_hour, t->tm_min, t->tm_sec, ext.c_str());
  return String(fileName);
}

void openNewCSV(){
  if(!SD_OK) return;
  currentFolder = createDailyFolder();
  currentCSVFile = generateTimeFile("csv");
  if(csvFile) csvFile.close();
  csvFile = SD.open(currentCSVFile, FILE_WRITE);
  if(!csvFile){ Serial.println("Failed to open CSV file!"); return; }
  Serial.print("CSV file created: "); Serial.println(currentCSVFile);
  csvFile.println("Timestamp;Peso;Roll;Pitch;Yaw;AX;AY;AZ;GX;GY;GZ;VBAT;BAT%");
  csvFile.flush();
}

void openNewTXT(){
  if(!SD_OK) return;
  currentFolder = createDailyFolder();
  currentTXTFile = generateTimeFile("txt");
  if(txtFile) txtFile.close();
  txtFile = SD.open(currentTXTFile, FILE_WRITE);
  if(!txtFile){ Serial.println("Failed to open TXT file!"); return; }
  Serial.print("TXT file created: "); Serial.println(currentTXTFile);
  txtFile.println("LOG START");
  txtFile.flush();
}

void logCSV(String line){ 
  if(csvFile){ 
    csvFile.println(line); 
    csvFile.flush(); 
    if(csvFile.size() > MAX_FILE_SIZE) openNewCSV(); 
  } 
}
void logTXT(String line){ 
  if(txtFile){ 
    txtFile.println(line); 
    txtFile.flush(); 
    if(txtFile.size() > MAX_FILE_SIZE) openNewTXT(); 
  } 
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
  d.ax=(buf[0]<<8)|buf[1]; d.ay=(buf[2]<<8)|buf[3]; d.az=(buf[4]<<8)|buf[5];
  d.gx=(buf[8]<<8)|buf[9]; d.gy=(buf[10]<<8)|buf[11]; d.gz=(buf[12]<<8)|buf[13];
}

void calculateOrientation(const IMUData &imu,float &roll,float &pitch,float &yaw,float dt){
  float ax=imu.ax/16384.0, ay=imu.ay/16384.0, az=imu.az/16384.0;
  float gx=imu.gx/131.0, gy=imu.gy/131.0, gz=imu.gz/131.0;

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
  pCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE);

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("BLE advertising started");
}

// ================= BLE SEND SPLIT =================
void sendBLE(String data){
  if(!deviceConnected || !pCharacteristic) return;

  const int chunkSize = 20; // BLE default max size
  int len = data.length();
  int offset = 0;

  while(offset < len){
    int size = min(chunkSize, len - offset);
    String chunk = data.substring(offset, offset + size);
    pCharacteristic->setValue(chunk.c_str());
    pCharacteristic->notify();
    offset += size;
    delay(5); // small delay for BLE stack stability
  }
}

// ================= SETUP =================
void setup(){
  Serial.begin(115200); delay(2000);
  pinMode(PIN_LED_R,OUTPUT); pinMode(PIN_LED_G,OUTPUT); pinMode(PIN_LED_B,OUTPUT);
  SPIbus.begin(PIN_SPI_SCK,PIN_SPI_MISO,PIN_SPI_MOSI,PIN_CS_IMU);
  pinMode(PIN_CS_IMU,OUTPUT); digitalWrite(PIN_CS_IMU,HIGH);

  // HX711
  scale.begin(PIN_HX_DT,PIN_HX_SCK);
  scale.set_scale(16338); scale.set_gain(128);
  Serial.println("Initializing HX711...");
  int retries=10; while(retries-- >0 && !scale.is_ready()){ Serial.println("Waiting for HX711..."); delay(500);}
  if(scale.is_ready()){ scale.tare(); Serial.println("HX711 ready"); } else Serial.println("HX711 not detected!");

  wifiSetup();
  sdInit();
  openNewCSV(); openNewTXT();
  bleSetup();
  tcpServer.begin();
}

// ================= LOOP =================
void loop(){
  static uint32_t lastTime = millis();
  uint32_t nowMillis = millis();
  float dt = (nowMillis - lastTime)/1000.0;
  lastTime = nowMillis;

  // IMU
  readIMU(imu);
  float roll,pitch,yaw;
  calculateOrientation(imu,roll,pitch,yaw,dt);

  // Weight
  float weight=NAN;
  if(scale.is_ready()){ unsigned long start = millis(); while(!scale.is_ready() && millis()-start<500); weight = scale.get_units(1); }

  // Battery
  float vbat = analogRead(PIN_BAT_ADC)/4095.0*3.3*2*0.943;
  int pct = max(0,min(100,(int)((vbat-3.2)/(4.2-3.2)*100)));

  // LED
  updateLEDs(pct);

  // Timestamp
  time_t now; time(&now); struct tm *t=localtime(&now);
  char timestamp[16]; sprintf(timestamp,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);

  String csvLine = String(timestamp)+";"+String(weight,2)+";"+
                   String(roll,2)+";"+String(pitch,2)+";"+String(yaw,2)+";"+
                   String(imu.ax/16384.0,3)+";"+String(imu.ay/16384.0,3)+";"+String(imu.az/16384.0,3)+";"+
                   String(imu.gx/131.0,3)+";"+String(imu.gy/131.0,3)+";"+String(imu.gz/131.0,3)+";"+
                   String(vbat,2)+";"+String(pct);

  // Log SD
  logCSV(csvLine); logTXT(csvLine);
  Serial.println(csvLine);

  // BLE send (full, split)
  sendBLE(csvLine);

  uint32_t elapsed = millis()-nowMillis;
  if(elapsed<50) delay(50-elapsed); // loop ~20 Hz
}
