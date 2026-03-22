#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "HX711.h"

struct IMUData;

// ================= ESP32-C3 PINOUT =================
// You can change if needed
#define PIN_SPI_SCK D6
#define PIN_SPI_MISO D5
#define PIN_SPI_MOSI D4
#define PIN_CS_IMU D7
#define PIN_CS_SD D8

#define PIN_HX_DT D10
#define PIN_HX_SCK D9

SPIClass SPIbus(FSPI);
HX711 scale;
const SPISettings IMU_SPI_SETTINGS(1000000, MSBFIRST, SPI_MODE3);
bool sdReady = false;
File logFile;
bool logFileReady = false;

// ===== MPU9250 REGISTERS =====
#define WHO_AM_I 0x75
#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B

// ===== SPI HELPERS =====
void writeRegister(uint8_t reg, uint8_t data)
{
  SPIbus.beginTransaction(IMU_SPI_SETTINGS);
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg);
  SPIbus.transfer(data);
  digitalWrite(PIN_CS_IMU, HIGH);
  SPIbus.endTransaction();
}

uint8_t readRegister(uint8_t reg)
{
  SPIbus.beginTransaction(IMU_SPI_SETTINGS);
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg | 0x80);
  uint8_t val = SPIbus.transfer(0x00);
  digitalWrite(PIN_CS_IMU, HIGH);
  SPIbus.endTransaction();
  return val;
}

void readBytes(uint8_t reg, uint8_t count, uint8_t *dest)
{
  SPIbus.beginTransaction(IMU_SPI_SETTINGS);
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg | 0x80);
  for (uint8_t i = 0; i < count; i++)
  {
    dest[i] = SPIbus.transfer(0x00);
  }
  digitalWrite(PIN_CS_IMU, HIGH);
  SPIbus.endTransaction();
}

struct IMUData
{
  int16_t ax;
  int16_t ay;
  int16_t az;
  int16_t gx;
  int16_t gy;
  int16_t gz;
};

void setupSPI()
{
  pinMode(PIN_CS_IMU, OUTPUT);
  pinMode(PIN_CS_SD, OUTPUT);
  digitalWrite(PIN_CS_IMU, HIGH);
  digitalWrite(PIN_CS_SD, HIGH);

  SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);
}

void setupMPU()
{
  writeRegister(PWR_MGMT_1, 0x00);
  delay(100);

  Serial.print("WHO_AM_I MPU9250: ");
  Serial.println(readRegister(WHO_AM_I), HEX);
}

void readIMU(IMUData &data)
{
  uint8_t buffer[14];
  readBytes(ACCEL_XOUT_H, 14, buffer);

  data.ax = (buffer[0] << 8) | buffer[1];
  data.ay = (buffer[2] << 8) | buffer[3];
  data.az = (buffer[4] << 8) | buffer[5];
  data.gx = (buffer[8] << 8) | buffer[9];
  data.gy = (buffer[10] << 8) | buffer[11];
  data.gz = (buffer[12] << 8) | buffer[13];
}

void setupHX711()
{
  scale.begin(PIN_HX_DT, PIN_HX_SCK);
  Serial.println("HX711 ready - reading raw values");
}

bool readLoadCell(float &weight)
{
  if (scale.is_ready())
  {
    long rawValue = scale.read_average(10);
    weight = (float)rawValue;
    return true;
  }
  else
  {
    return false;
  }
}

void setupSD()
{
  sdReady = SD.begin(PIN_CS_SD, SPIbus);
  if (!sdReady)
  {
    Serial.println("SD init failed");
    return;
  }

  bool newFile = !SD.exists("/imu_load.csv");
  logFile = SD.open("/imu_load.csv", FILE_WRITE);
  if (!logFile)
  {
    Serial.println("SD open failed");
    return;
  }

  if (newFile)
  {
    logFile.println("ms,ax,ay,az,gx,gy,gz,load");
    logFile.flush();
  }

  logFileReady = true;
}

void logToSD(uint32_t nowMs, const IMUData &imu, float weight)
{
  if (!sdReady || !logFileReady)
  {
    return;
  }

  logFile.seek(logFile.size());

  logFile.print(nowMs);
  logFile.print(',');
  logFile.print(imu.ax);
  logFile.print(',');
  logFile.print(imu.ay);
  logFile.print(',');
  logFile.print(imu.az);
  logFile.print(',');
  logFile.print(imu.gx);
  logFile.print(',');
  logFile.print(imu.gy);
  logFile.print(',');
  logFile.print(imu.gz);
  logFile.print(',');
  if (isnan(weight))
  {
    logFile.println("NaN");
  }
  else
  {
    logFile.println(weight, 3);
  }
  logFile.flush();

  Serial.print("SD: ");
  Serial.print(nowMs);
  Serial.print(',');
  Serial.print(imu.ax);
  Serial.print(',');
  Serial.print(imu.ay);
  Serial.print(',');
  Serial.print(imu.az);
  Serial.print(',');
  Serial.print(imu.gx);
  Serial.print(',');
  Serial.print(imu.gy);
  Serial.print(',');
  Serial.print(imu.gz);
  Serial.print(',');
  if (isnan(weight))
  {
    Serial.println("NaN");
  }
  else
  {
    Serial.println(weight, 3);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);

  setupSPI();
  setupMPU();
  setupHX711();
  setupSD();
}

void loop()
{
  IMUData imu;
  float weight = NAN;
  readIMU(imu);
  bool loadOk = readLoadCell(weight);

  Serial.print("ACCEL: ");
  Serial.print(imu.ax);
  Serial.print(", ");
  Serial.print(imu.ay);
  Serial.print(", ");
  Serial.print(imu.az);

  Serial.print(" | GYRO: ");
  Serial.print(imu.gx);
  Serial.print(", ");
  Serial.print(imu.gy);
  Serial.print(", ");
  Serial.print(imu.gz);

  Serial.print(" | LOADCELL: ");
  if (loadOk)
  {
    Serial.println(weight, 3);
  }
  else
  {
    Serial.println("not ready");
  }

  logToSD(millis(), imu, weight);
  delay(100);
}