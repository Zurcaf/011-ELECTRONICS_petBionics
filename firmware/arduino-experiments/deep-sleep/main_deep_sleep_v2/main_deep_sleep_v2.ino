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
const char* WIFI_SSID = "composites";
const char* WIFI_PASS = "Composites2019";
bool timeSynced = false;

// ================= BLE =================
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override { deviceConnected = true; Serial.println("BLE connected"); }
  void onDisconnect(BLEServer* pServer) override { deviceConnected = false; pServer->getAdvertising()->start(); }
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
File txtFile;
String currentFolder = "";
const size_t MAX_FILE_SIZE = 32*1024; // 32 KB

// ================= LED =================
void setLED(bool r,bool g,bool b){ digitalWrite(PIN_LED_R,r); digitalWrite(PIN_LED_G,g); digitalWrite(PIN_LED_B,b); }

uint32_t ledLastToggle = 0;
bool redState = false;

void updateLEDs(float batteryPct) {
  bool green = timeSynced;
  bool yellow = deviceConnected;
  bool red = false;
  uint32_t now = millis();

  if (batteryPct < 10) { if (now - ledLastToggle >= 200) { redState = !redState; ledLastToggle = now; } red = redState; }
  else if (batteryPct < 15) { if (now - ledLastToggle >= 500) { redState = !redState; ledLastToggle = now; } red = redState; }
  else { red = false; redState = false; }

  setLED(red, green, yellow);
}

// ================= WIFI + NTP =================
void wifiSetup(){
  timeSynced = false;
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false); WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  uint32_t start = millis();
  while(WiFi.status() != WL_CONNECTED && millis()-start < 15000){ delay(500); Serial.print("."); }

  if(WiFi.status() == WL_CONNECTED){
    Serial.println("\nWiFi connected");
    configTime(0, 0, "pool.ntp.org");
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) { 
      Serial.println("Time synchronized"); timeSynced = true; updateLEDs(100); delay(2000); timeSynced = false; updateLEDs(100); 
    }
    WiFi.disconnect(true,true); delay(100); WiFi.mode(WIFI_OFF); Serial.println("WiFi disabled");
  }
}

// ================= SD =================
void sdInit(){ SD_OK = SD.begin(PIN_CS_SD, SPIbus); if(!SD_OK){ Serial.println("SD FAILED"); return; } Serial.println("SD OK"); }

// ================= FILE =================
String createDailyFolder(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "/LOGS/NO_TIME";
  char path[32]; sprintf(path,"/LOGS/%04d-%02d-%02d",timeinfo.tm_year+1900,timeinfo.tm_mon+1,timeinfo.tm_mday);
  if(!SD.exists("/LOGS")) SD.mkdir("/LOGS");
  if(!SD.exists(path)) SD.mkdir(path);
  return String(path);
}

String generateTimeFile(String ext){
  struct timeval tv; gettimeofday(&tv,NULL);
  struct tm timeinfo; localtime_r(&tv.tv_sec,&timeinfo);
  char fileName[64];
  sprintf(fileName,"%s/%02d-%02d-%02d.%03ld.%s",
          currentFolder.c_str(),timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,tv.tv_usec/1000,ext.c_str());
  return String(fileName);
}

void openNewCSV(){
  if(!SD_OK) return;
  String csvFileName = generateTimeFile("csv"); 
  if(csvFile) csvFile.close();
  csvFile = SD.open(csvFileName, FILE_WRITE);
  if(csvFile){ 
    csvFile.println("Timestamp;Peso;Roll;Pitch;Yaw;AX;AY;AZ;GX;GY;GZ;VBAT;BAT%");
    csvFile.flush(); 
  } else Serial.println("CSV open failed");
}

void openNewTXT(){
  if(!SD_OK) return;
  String txtFileName = generateTimeFile("txt"); 
  if(txtFile) txtFile.close();
  txtFile = SD.open(txtFileName, FILE_WRITE);
  if(txtFile){ 
    txtFile.println("====== LOG DATA ======");
    txtFile.println("Format: Timestamp | Weight(kg) | Roll | Pitch | Yaw | AX | AY | AZ | GX | GY | GZ | VBAT(V) | BAT%");
    txtFile.println("=====================");
    txtFile.flush();
  } else Serial.println("TXT open failed");
}

void openNewCSVandTXT(){
  currentFolder = createDailyFolder();
  openNewCSV();
  openNewTXT();
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
  uint8_t buf[14]; readBytes(ACCEL_XOUT_H,14,buf);
  d.ax=(buf[0]<<8)|buf[1]; d.ay=(buf[2]<<8)|buf[3]; d.az=(buf[4]<<8)|buf[5];
  d.gx=(buf[8]<<8)|buf[9]; d.gy=(buf[10]<<8)|buf[11]; d.gz=(buf[12]<<8)|buf[13];
}

void calculateOrientation(const IMUData &imu,float &roll,float &pitch,float &yaw,float dt){
  float ax=imu.ax/16384.0, ay=imu.ay/16384.0, az=imu.az/16384.0;
  float gx=imu.gx/131.0, gy=imu.gy/131.0, gz=imu.gz/131.0;
  roll = atan2(ay,az)*180.0/PI;
  pitch = atan2(-ax,sqrt(ay*ay+az*az))*180.0/PI;
  yawAngle += gz*dt; if(yawAngle>180) yawAngle-=360; if(yawAngle<-180) yawAngle+=360;
  yaw = yawAngle;
}

// ================= BLE =================
void bleSetup(){
  BLEDevice::init("ESP32-LOGGER");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID,BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  pServer->getAdvertising()->start();
}

void sendBLE(String data){
  if(!deviceConnected) return;
  const int chunkSize = 20;
  for(int i=0;i<data.length();i+=chunkSize){
    pCharacteristic->setValue(data.substring(i,i+chunkSize).c_str());
    pCharacteristic->notify(); delay(2);
  }
}

// ================= CSV + TXT helpers =================
void writeCSVLine(const char* line){ 
  if(csvFile){ 
    csvFile.println(line); 
    csvFile.flush(); 
    if(csvFile.size()>=MAX_FILE_SIZE) openNewCSV();
  } 
}

void writeTXTLine(const char* timestamp,float weight,float roll,float pitch,float yaw,float ax,float ay,float az,float gx,float gy,float gz,float vbat,int pct){
  if(!txtFile) return;
  char line[200];
  sprintf(line,"%s | W: %.2f kg | R: %.2f° | P: %.2f° | Y: %.2f° | AX: %.3f | AY: %.3f | AZ: %.3f | GX: %.3f | GY: %.3f | GZ: %.3f | VBAT: %.2f V | BAT%%: %d",
          timestamp,weight,roll,pitch,yaw,ax,ay,az,gx,gy,gz,vbat,pct);
  txtFile.println(line); 
  txtFile.flush();
  if(txtFile.size()>=MAX_FILE_SIZE) openNewTXT();
}

// ================= LOGGING & SLEEP =================
#define LOG_DURATION_MS 180000   // 3 minutes
#define SLEEP_DURATION_S 120     // 2 minutes
const uint32_t LOOP_PERIOD_US = 50000; // 50 ms = 20 Hz

// ================= SETUP =================
void setup(){
  Serial.begin(115200); delay(2000);
  pinMode(PIN_LED_R,OUTPUT); pinMode(PIN_LED_G,OUTPUT); pinMode(PIN_LED_B,OUTPUT);
  SPIbus.begin(PIN_SPI_SCK,PIN_SPI_MISO,PIN_SPI_MOSI,PIN_CS_IMU);
  pinMode(PIN_CS_IMU,OUTPUT); digitalWrite(PIN_CS_IMU,HIGH);
  scale.begin(PIN_HX_DT,PIN_HX_SCK); scale.set_scale(16338); scale.tare();
  wifiSetup(); sdInit(); openNewCSVandTXT(); bleSetup();
}

// ================= LOOP =================
void loop(){
  static uint32_t loggingStartTime = millis();
  static uint64_t nextLoopTime = micros(); // precise 20Hz

  uint64_t now = micros();
  if(now >= nextLoopTime){
    nextLoopTime += LOOP_PERIOD_US; // schedule next reading
    float dt = LOOP_PERIOD_US / 1000000.0;

    // --- Sensor Readings ---
    readIMU(imu); 
    float roll,pitch,yaw; 
    calculateOrientation(imu,roll,pitch,yaw,dt);
    float weight = scale.is_ready()?scale.get_units(1):NAN;
    float vbat = analogRead(PIN_BAT_ADC)/4095.0*3.3*2*0.943;
    int pct = max(0,min(100,(int)((vbat-3.2)/(4.2-3.2)*100)));
    updateLEDs(pct);

    // --- Timestamp ---
    struct timeval tv; gettimeofday(&tv,NULL);
    struct tm timeinfo; localtime_r(&tv.tv_sec,&timeinfo);
    char timestamp[24]; sprintf(timestamp,"%02d:%02d:%02d.%03ld",timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec,tv.tv_usec/1000);

    // --- Save CSV / TXT ---
    String csvLine = String(timestamp)+";"+String(weight,2)+";"+String(roll,2)+";"+String(pitch,2)+";"+String(yaw,2)+";"+
                     String(imu.ax/16384.0,3)+";"+String(imu.ay/16384.0,3)+";"+String(imu.az/16384.0,3)+";"+
                     String(imu.gx/131.0,3)+";"+String(imu.gy/131.0,3)+";"+String(imu.gz/131.0,3)+";"+
                     String(vbat,2)+";"+String(pct);

    writeCSVLine(csvLine.c_str());
    writeTXTLine(timestamp,weight,roll,pitch,yaw,imu.ax/16384.0,imu.ay/16384.0,imu.az/16384.0,
                 imu.gx/131.0,imu.gy/131.0,imu.gz/131.0,vbat,pct);

    // --- BLE ---
    sendBLE(csvLine); 
    Serial.println(csvLine);
  }

  // --- Check if logging time exceeded ---
  if(millis() - loggingStartTime >= LOG_DURATION_MS){
    Serial.println("Logging period complete. Going to deep sleep...");
    if(csvFile) csvFile.close();
    if(txtFile) txtFile.close();
    delay(100);
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION_S * 1000000ULL);
    esp_deep_sleep_start();
  }
}
