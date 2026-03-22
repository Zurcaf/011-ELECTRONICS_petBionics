#include <SPI.h>
#include "HX711.h"

// ================= ESP32-C3 PINOUT =================
// You can change if needed
#define PIN_SPI_SCK   D9
#define PIN_SPI_MISO  D10
#define PIN_SPI_MOSI  D8
#define PIN_CS_IMU    D5

#define PIN_HX_DT  D4
#define PIN_HX_SCK D3

SPIClass SPIbus(FSPI);
HX711 scale;

// ===== MPU9250 REGISTERS =====
#define WHO_AM_I 0x75
#define PWR_MGMT_1 0x6B
#define ACCEL_XOUT_H 0x3B

// ===== SPI HELPERS =====
void writeRegister(uint8_t reg, uint8_t data) {
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg);
  SPIbus.transfer(data);
  digitalWrite(PIN_CS_IMU, HIGH);
}

uint8_t readRegister(uint8_t reg) {
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg | 0x80);
  uint8_t val = SPIbus.transfer(0x00);
  digitalWrite(PIN_CS_IMU, HIGH);
  return val;
}

void readBytes(uint8_t reg, uint8_t count, uint8_t* dest) {
  digitalWrite(PIN_CS_IMU, LOW);
  SPIbus.transfer(reg | 0x80);
  for (uint8_t i = 0; i < count; i++) {
    dest[i] = SPIbus.transfer(0x00);
  }
  digitalWrite(PIN_CS_IMU, HIGH);
}

void setupMPU() {
  pinMode(PIN_CS_IMU, OUTPUT);
  digitalWrite(PIN_CS_IMU, HIGH);

  SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);
  SPIbus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));

  writeRegister(PWR_MGMT_1, 0x00);
  delay(100);

  Serial.print("WHO_AM_I MPU9250: ");
  Serial.println(readRegister(WHO_AM_I), HEX);
}

void readIMU() {
  uint8_t buffer[14];
  readBytes(ACCEL_XOUT_H, 14, buffer);

  int16_t ax = (buffer[0] << 8) | buffer[1];
  int16_t ay = (buffer[2] << 8) | buffer[3];
  int16_t az = (buffer[4] << 8) | buffer[5];

  int16_t gx = (buffer[8] << 8) | buffer[9];
  int16_t gy = (buffer[10] << 8) | buffer[11];
  int16_t gz = (buffer[12] << 8) | buffer[13];

  Serial.print("ACCEL: ");
  Serial.print(ax); Serial.print(", ");
  Serial.print(ay); Serial.print(", ");
  Serial.print(az);

  Serial.print(" | GYRO: ");
  Serial.print(gx); Serial.print(", ");
  Serial.print(gy); Serial.print(", ");
  Serial.println(gz);
}

void setupHX711() {
  scale.begin(PIN_HX_DT, PIN_HX_SCK);
  scale.set_scale();
  scale.tare();
}

void readLoadCell() {
  if (scale.is_ready()) {
    float weight = scale.get_units(5);
    Serial.print("LOADCELL: ");
    Serial.println(weight);
  } else {
    Serial.println("HX711 not ready");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  setupMPU();
  setupHX711();
}

void loop() {
  // readIMU();
  readLoadCell();
  delay(100);
}