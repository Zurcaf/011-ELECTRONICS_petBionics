//
// Gyroscope bias calibration sketch for petBionic
//
// The gyroscope should read zero on all axes when stationary, regardless of
// orientation. Any non-zero average is bias that causes yaw drift over time.
//
// Note: accelerometer bias calibration requires a known orientation (e.g. flat
// on a table) because you need to know what the expected reading should be.
// The MPU-9250 factory accel calibration is typically good enough (<60 mg).
//
// Instructions:
//   1. Leave device STILL in any position
//   2. Upload and open Serial Monitor (115200 baud)
//   3. Press ENTER to start collecting 600 samples (~6 seconds)
//   4. Do NOT move the device during the countdown
//   5. Copy the printed values into OrientationEstimator.h
//
// Pins: SCK=D6, MISO=D5, MOSI=D4, CS=D7  (mirrors Pinout.h)
//

#include <SPI.h>

// ---- Pins ----
#define PIN_SCK  D6
#define PIN_MISO D5
#define PIN_MOSI D4
#define PIN_CS   D7

// ---- MPU-9250 registers ----
#define MPU_WHO_AM_I   0x75
#define MPU_PWR_MGMT_1 0x6B
#define MPU_ACCEL_XOUT 0x3B  // 6 bytes accel (big-endian XYZ)
#define MPU_GYRO_XOUT  0x43  // 6 bytes gyro  (big-endian XYZ)

// ---- Scale factors (must match OrientationEstimator.h) ----
static constexpr float kAccelScale = 1.0f / 16384.0f; // ±2 g
static constexpr float kGyroScale  = 1.0f / 131.0f;   // ±250 °/s

static constexpr int kNumSamples = 600; // ~6 seconds at 100 Hz

SPIClass g_spi(FSPI);

// ---- SPI helpers ----

void imuWrite(uint8_t reg, uint8_t value)
{
  g_spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(PIN_CS, LOW);
  g_spi.transfer(reg);
  g_spi.transfer(value);
  digitalWrite(PIN_CS, HIGH);
  g_spi.endTransaction();
}

uint8_t imuRead1(uint8_t reg)
{
  g_spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(PIN_CS, LOW);
  g_spi.transfer(reg | 0x80);
  uint8_t val = g_spi.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  g_spi.endTransaction();
  return val;
}

void imuReadN(uint8_t reg, uint8_t n, uint8_t *dst)
{
  g_spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(PIN_CS, LOW);
  g_spi.transfer(reg | 0x80);
  for (uint8_t i = 0; i < n; i++) dst[i] = g_spi.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  g_spi.endTransaction();
}

// ---- setup ----

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n===== GYROSCOPE BIAS CALIBRATION =====");
  Serial.println("Deixa o dispositivo IMOVIL em qualquer posicao.");
  Serial.println("Pressiona ENTER para iniciar...\n");

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  g_spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  delay(100);

  imuWrite(MPU_PWR_MGMT_1, 0x00);
  delay(100);

  uint8_t who = imuRead1(MPU_WHO_AM_I);
  Serial.printf("[DIAG] WHO_AM_I = 0x%02X (esperado 0x71 ou 0x73)\n", who);
  if (who == 0x00 || who == 0xFF)
  {
    Serial.println("[ERRO] MPU-9250 nao responde! Verifica SPI e alimentacao.");
    while (1);
  }

  while (!Serial.available()) { delay(50); }
  while (Serial.available()) Serial.read();

  Serial.printf("\nA recolher %d amostras — NAO mexas no dispositivo...\n\n", kNumSamples);

  runCalibration();

  Serial.println("\nDone. Reinicia para repetir.");
  while (1);
}

void loop() {}

// ---- calibration ----

void runCalibration()
{
  double sumAx = 0, sumAy = 0, sumAz = 0;
  double sumGx = 0, sumGy = 0, sumGz = 0;

  for (int i = 0; i < kNumSamples; i++)
  {
    uint8_t buf[12];
    imuReadN(MPU_ACCEL_XOUT, 6, buf);
    imuReadN(MPU_GYRO_XOUT,  6, buf + 6);

    sumAx += (int16_t)((buf[0]  << 8) | buf[1]);
    sumAy += (int16_t)((buf[2]  << 8) | buf[3]);
    sumAz += (int16_t)((buf[4]  << 8) | buf[5]);
    sumGx += (int16_t)((buf[6]  << 8) | buf[7]);
    sumGy += (int16_t)((buf[8]  << 8) | buf[9]);
    sumGz += (int16_t)((buf[10] << 8) | buf[11]);

    if ((i + 1) % 60 == 0)
      Serial.printf("  %d/%d...\n", i + 1, kNumSamples);

    delay(10);
  }

  // ---- Gyro bias: average should be 0 when stationary ----
  float gbx = (float)(sumGx / kNumSamples) * kGyroScale;
  float gby = (float)(sumGy / kNumSamples) * kGyroScale;
  float gbz = (float)(sumGz / kNumSamples) * kGyroScale;

  // ---- Accel sanity: magnitude should be ~1g (device was still) ----
  float ax_g = (float)(sumAx / kNumSamples) * kAccelScale;
  float ay_g = (float)(sumAy / kNumSamples) * kAccelScale;
  float az_g = (float)(sumAz / kNumSamples) * kAccelScale;
  float mag  = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);

  Serial.println();
  Serial.printf("[DIAG] |accel| = %.4f g (esperado ~1.000 — confirma que estava imovil)\n", mag);
  if (fabsf(mag - 1.0f) > 0.05f)
    Serial.println("[AVISO] Magnitude longe de 1g — o dispositivo estava a mexer durante a coleta?");

  Serial.println("\n===== RESULTADO =====\n");
  Serial.printf("Gyro bias medido: gx=%+.4f  gy=%+.4f  gz=%+.4f deg/s\n\n", gbx, gby, gbz);

  Serial.println("Copia para OrientationEstimator.h:");
  Serial.printf("  static constexpr float kGyroOffsetX = %.4ff;\n", gbx);
  Serial.printf("  static constexpr float kGyroOffsetY = %.4ff;\n", gby);
  Serial.printf("  static constexpr float kGyroOffsetZ = %.4ff;\n", gbz);
}
