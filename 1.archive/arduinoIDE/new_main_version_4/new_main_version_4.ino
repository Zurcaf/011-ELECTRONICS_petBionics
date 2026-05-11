#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "HX711.h"

struct IMUData;

// ================= ESP32-C3 PINOUT =================
#define PIN_SPI_SCK D6
#define PIN_SPI_MISO D5
#define PIN_SPI_MOSI D4
#define PIN_CS_IMU D7
#define PIN_CS_SD D8

#define PIN_HX_DT D10
#define PIN_HX_SCK D9

// ===== RGB LED PINS =====
#define PIN_LED_R D1
#define PIN_LED_G D2
#define PIN_LED_B D3

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

// ===== LED ENUM =====
enum LEDColor
{
  LED_OFF,
  LED_RED,
  LED_GREEN,
  LED_BLUE
};

LEDColor currentLED = LED_OFF;
unsigned long lastLEDChange = 0;
const unsigned long LED_INTERVAL = 1000;  // 1 second

// ===================================================
// ================= LED CONTROL =====================
// ===================================================

void setLED(LEDColor color)
{
  currentLED = color;

  digitalWrite(PIN_LED_R, LOW);
  digitalWrite(PIN_LED_G, LOW);
  digitalWrite(PIN_LED_B, LOW);

  switch (color)
  {
    case LED_RED:   digitalWrite(PIN_LED_R, HIGH); break;
    case LED_GREEN: digitalWrite(PIN_LED_G, HIGH); break;
    case LED_BLUE:  digitalWrite(PIN_LED_B, HIGH); break;
    default: break;
  }
}

void updateLED()
{
  if (millis() - lastLEDChange >= LED_INTERVAL)
  {
    lastLEDChange = millis();

    switch (currentLED)
    {
      case LED_OFF:   setLED(LED_RED); break;
      case LED_RED:   setLED(LED_GREEN); break;
      case LED_GREEN: setLED(LED_BLUE); break;
      case LED_BLUE:  setLED(LED_OFF); break;
    }
  }
}

// ===================================================
// ================= IMU FUNCTIONS ===================
// ===================================================

void writeRegister(uint8_t reg, uint8_t data)
{
  SPIbus.beginTransaction(IMU_SPI_SETTINGS);
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg);
  SPIbus.transfer(data);
  digitalWrite(PIN_CS_IMU, HIGH);
  SPIbus.endTransaction();
}

void readBytes(uint8_t reg, uint8_t count, uint8_t *dest)
{
  SPIbus.beginTransaction(IMU_SPI_SETTINGS);
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg | 0x80);
  for (uint8_t i = 0; i < count; i++)
    dest[i] = SPIbus.transfer(0x00);
  digitalWrite(PIN_CS_IMU, HIGH);
  SPIbus.endTransaction();
}

struct IMUData
{
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
};

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

// ===================================================
// ================= LOAD CELL =======================
// ===================================================

bool readLoadCell(float &weight)
{
  if (scale.is_ready())
  {
    long rawValue = scale.read_average(10);
    weight = (float)rawValue;
    return true;
  }
  return false;
}

// ===================================================
// ================= SD LOGGING ======================
// ===================================================

void setupSD()
{
  sdReady = SD.begin(PIN_CS_SD, SPIbus);
  if (!sdReady)
  {
    Serial.println("SD init failed");
    return;
  }

  bool newFile = !SD.exists("/imu_load_led.csv");
  logFile = SD.open("/imu_load_led.csv", FILE_WRITE);

  if (!logFile)
  {
    Serial.println("SD open failed");
    return;
  }

  if (newFile)
  {
    logFile.println("ms,ax,ay,az,gx,gy,gz,load,led");
    logFile.flush();
  }

  logFileReady = true;
}

void logToSD(uint32_t nowMs, const IMUData &imu, float weight)
{
  if (!sdReady || !logFileReady)
    return;

  logFile.seek(logFile.size());

  logFile.print(nowMs); logFile.print(',');
  logFile.print(imu.ax); logFile.print(',');
  logFile.print(imu.ay); logFile.print(',');
  logFile.print(imu.az); logFile.print(',');
  logFile.print(imu.gx); logFile.print(',');
  logFile.print(imu.gy); logFile.print(',');
  logFile.print(imu.gz); logFile.print(',');

  if (isnan(weight))
    logFile.print("NaN");
  else
    logFile.print(weight, 3);

  logFile.print(',');

  switch (currentLED)
  {
    case LED_RED:   logFile.println("RED"); break;
    case LED_GREEN: logFile.println("GREEN"); break;
    case LED_BLUE:  logFile.println("BLUE"); break;
    default:        logFile.println("OFF"); break;
  }

  logFile.flush();
}

// ===================================================
// ================= SETUP ===========================
// ===================================================

void setup()
{
  Serial.begin(115200);
  delay(2000);

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setLED(LED_OFF);

  SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);
  pinMode(PIN_CS_IMU, OUTPUT);
  pinMode(PIN_CS_SD, OUTPUT);
  digitalWrite(PIN_CS_IMU, HIGH);
  digitalWrite(PIN_CS_SD, HIGH);

  scale.begin(PIN_HX_DT, PIN_HX_SCK);

  setupSD();
}

// ===================================================
// ================= LOOP ============================
// ===================================================

void loop()
{
  IMUData imu;
  float weight = NAN;

  readIMU(imu);
  readLoadCell(weight);

  updateLED();  // Independent LED

  // ===== SERIAL PRINT ALL VALUES =====
  Serial.print("ACCEL: ");
  Serial.print(imu.ax); Serial.print(", ");
  Serial.print(imu.ay); Serial.print(", ");
  Serial.print(imu.az);

  Serial.print(" | GYRO: ");
  Serial.print(imu.gx); Serial.print(", ");
  Serial.print(imu.gy); Serial.print(", ");
  Serial.print(imu.gz);

  Serial.print(" | LOAD: ");
  if (isnan(weight))
    Serial.print("NaN");
  else
    Serial.print(weight, 3);

  Serial.print(" | LED: ");
  switch (currentLED)
  {
    case LED_RED: Serial.println("RED"); break;
    case LED_GREEN: Serial.println("GREEN"); break;
    case LED_BLUE: Serial.println("BLUE"); break;
    default: Serial.println("OFF"); break;
  }

  logToSD(millis(), imu, weight);

  delay(100);
}
