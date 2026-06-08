//
// Magnetometer calibration sketch for petBionic (MPU-9250 via SPI + AK8963 via I2C master)
//
// Pins: SCK=D6, MISO=D5, MOSI=D4, CS=D7  (mirrors Pinout.h)
// AK8963 is accessed through the MPU-9250's internal I2C master, NOT via Wire.
//

#include <SPI.h>

// ---- SPI / IMU pins (from Pinout.h) ----
#define PIN_SCK  D6
#define PIN_MISO D5
#define PIN_MOSI D4
#define PIN_CS   D7

// ---- MPU-9250 registers ----
#define MPU_WHO_AM_I     0x75
#define MPU_PWR_MGMT_1   0x6B
#define MPU_USER_CTRL    0x6A
#define MPU_I2C_MST_CTRL 0x24
#define MPU_SLV0_ADDR    0x25
#define MPU_SLV0_REG     0x26
#define MPU_SLV0_CTRL    0x27
#define MPU_SLV0_DO      0x63
#define MPU_EXT_DATA_00  0x49

// ---- AK8963 registers / constants ----
#define AK_ADDR    0x0C
#define AK_WIA     0x00
#define AK_ST1     0x02
#define AK_CNTL1   0x0A
#define AK_WHO_VAL 0x48

// ---- Calibration accumulators ----
float mx_min = 9999, mx_max = -9999;
float my_min = 9999, my_max = -9999;
float mz_min = 9999, mz_max = -9999;
uint32_t sample_count = 0;
uint32_t start_time = 0;

SPIClass g_spi(FSPI);

// ---- Low-level SPI helpers (mirrors RawSensor.cpp) ----

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

// ---- AK8963 helpers via I2C master (mirrors RawSensor.cpp) ----

void akWrite(uint8_t reg, uint8_t value)
{
  imuWrite(MPU_SLV0_ADDR, AK_ADDR);         // slave address, write mode
  imuWrite(MPU_SLV0_REG,  reg);
  imuWrite(MPU_SLV0_DO,   value);
  imuWrite(MPU_SLV0_CTRL, 0x81);            // enable, 1 byte
  delay(2);
  imuWrite(MPU_SLV0_CTRL, 0x00);            // disable
}

bool akRead(uint8_t reg, uint8_t n, uint8_t *dst)
{
  imuWrite(MPU_SLV0_ADDR, AK_ADDR | 0x80); // slave address, read mode
  imuWrite(MPU_SLV0_REG,  reg);
  imuWrite(MPU_SLV0_CTRL, 0x80 | n);        // enable, n bytes
  delay(2);
  imuReadN(MPU_EXT_DATA_00, n, dst);
  imuWrite(MPU_SLV0_CTRL, 0x00);
  return true;
}

// ---- Read one calibrated magnetometer sample ----

bool readMag(float &mx, float &my, float &mz)
{
  uint8_t raw[8];
  if (!akRead(AK_ST1, 8, raw)) return false;

  const bool drdy     = (raw[0] & 0x01) != 0;
  const bool overflow = (raw[7] & 0x08) != 0;

  if (!drdy || overflow) return false;

  // Little-endian: L byte first, then H byte
  int16_t rmx = (int16_t)((raw[2] << 8) | raw[1]);
  int16_t rmy = (int16_t)((raw[4] << 8) | raw[3]);
  int16_t rmz = (int16_t)((raw[6] << 8) | raw[5]);

  mx = rmx * 0.15f; // 0.15 µT/LSB in 16-bit mode
  my = rmy * 0.15f;
  mz = rmz * 0.15f;
  return true;
}

// ---- setup ----

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n===== MAGNETOMETER CALIBRATION (SPI) =====");

  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  g_spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CS);
  delay(100);

  // Wake up MPU-9250
  imuWrite(MPU_PWR_MGMT_1, 0x00);
  delay(100);

  uint8_t whoAmI = imuRead1(MPU_WHO_AM_I);
  Serial.printf("[DIAG] MPU WHO_AM_I = 0x%02X (esperado 0x71 ou 0x73)\n", whoAmI);
  if (whoAmI == 0x00 || whoAmI == 0xFF)
  {
    Serial.println("[ERRO] MPU-9250 nao responde! Verifica ligacoes SPI e alimentacao.");
    while (1);
  }

  // Enable I2C master mode so MPU can talk to AK8963
  imuWrite(MPU_USER_CTRL,    0x20); // enable I2C master
  delay(10);
  imuWrite(MPU_I2C_MST_CTRL, 0x00); // 348 kHz clock
  delay(10);

  // Check AK8963 WHO_AM_I
  uint8_t akId = 0;
  akRead(AK_WIA, 1, &akId);
  Serial.printf("[DIAG] AK8963 WHO_AM_I = 0x%02X (esperado 0x48)\n", akId);
  if (akId != AK_WHO_VAL)
  {
    Serial.println("[ERRO] AK8963 nao detectado! Chip pode ser clone sem magnetometro interno.");
    while (1);
  }

  // Initialize AK8963: power down then continuous mode 2 16-bit
  akWrite(AK_CNTL1, 0x00);
  delay(30);
  akWrite(AK_CNTL1, 0x16);
  delay(30);

  uint8_t cntl1 = 0;
  akRead(AK_CNTL1, 1, &cntl1);
  Serial.printf("[DIAG] AK8963 CNTL1 = 0x%02X (esperado 0x16)\n", cntl1);

  // Quick read test
  Serial.println("[DIAG] Teste de leitura (5 amostras):");
  for (int i = 0; i < 5; i++)
  {
    float tx, ty, tz;
    if (readMag(tx, ty, tz))
      Serial.printf("  %d: mx=%.1f my=%.1f mz=%.1f uT\n", i, tx, ty, tz);
    else
      Serial.printf("  %d: sem dados (DRDY=0 ou overflow)\n", i);
    delay(20);
  }

  Serial.println("\n==========================================");
  Serial.println("Instrucoes:");
  Serial.println("1. Roda o dispositivo LENTAMENTE em figura-de-8");
  Serial.println("2. Faz isso em TODOS os 3 planos (XY, XZ, YZ)");
  Serial.println("3. Leva ~2-3 minutos");
  Serial.println("4. Serial mostra os valores min/max a cada segundo");
  Serial.println("5. Pressiona ENTER quando terminar");
  Serial.println("==========================================\n");

  start_time = millis();
}

// ---- loop ----

void loop()
{
  float mx, my, mz;
  if (readMag(mx, my, mz))
  {
    if (mx < mx_min) mx_min = mx;
    if (mx > mx_max) mx_max = mx;
    if (my < my_min) my_min = my;
    if (my > my_max) my_max = my;
    if (mz < mz_min) mz_min = mz;
    if (mz > mz_max) mz_max = mz;
    sample_count++;

    if (sample_count % 80 == 0)
    {
      uint32_t elapsed = (millis() - start_time) / 1000;
      Serial.printf("[%03d seg] MX: %.1f~%.1f | MY: %.1f~%.1f | MZ: %.1f~%.1f | n=%u\n",
                    elapsed, mx_min, mx_max, my_min, my_max, mz_min, mz_max, sample_count);
    }
  }

  if (Serial.available())
  {
    Serial.read();
    printCalibration();
    while (1);
  }

  delay(12); // ~80 Hz
}

// ---- calibration output ----

void printCalibration()
{
  Serial.println("\n\n===== CALIBRATION COMPLETE =====\n");

  if (sample_count == 0)
  {
    Serial.println("ERRO: Nenhuma amostra! Verifica ligacoes.");
    return;
  }

  float mx_offset = (mx_max + mx_min) / 2.0f;
  float my_offset = (my_max + my_min) / 2.0f;
  float mz_offset = (mz_max + mz_min) / 2.0f;

  // Sphere normalization: scale each axis so all three have the same radius (avg).
  // This preserves physical units (µT) and keeps tilt compensation correct.
  float amp_x = mx_max - mx_min;
  float amp_y = my_max - my_min;
  float amp_z = mz_max - mz_min;
  float avg_amp = (amp_x + amp_y + amp_z) / 3.0f;
  float mx_scale  = avg_amp / amp_x;  // should be ~1.0
  float my_scale  = avg_amp / amp_y;
  float mz_scale  = avg_amp / amp_z;

  Serial.println("Raw min/max:");
  Serial.printf("  MX: %.1f ~ %.1f  (amplitude %.1f)\n", mx_min, mx_max, mx_max - mx_min);
  Serial.printf("  MY: %.1f ~ %.1f  (amplitude %.1f)\n", my_min, my_max, my_max - my_min);
  Serial.printf("  MZ: %.1f ~ %.1f  (amplitude %.1f)\n\n", mz_min, mz_max, mz_max - mz_min);
  Serial.printf("Total amostras: %u\n\n", sample_count);

  Serial.println("Copia para OrientationEstimator.h:");
  Serial.println("  // OFFSETS (hard-iron)");
  Serial.printf("  static constexpr float kMagOffsetX = %.2ff;\n", mx_offset);
  Serial.printf("  static constexpr float kMagOffsetY = %.2ff;\n", my_offset);
  Serial.printf("  static constexpr float kMagOffsetZ = %.2ff;\n", mz_offset);
  Serial.println("  // SCALES (soft-iron)");
  Serial.printf("  static constexpr float kMagScaleX  = %.4ff;\n", mx_scale);
  Serial.printf("  static constexpr float kMagScaleY  = %.4ff;\n", my_scale);
  Serial.printf("  static constexpr float kMagScaleZ  = %.4ff;\n", mz_scale);
}
