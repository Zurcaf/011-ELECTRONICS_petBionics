// Debug: continuous Roll/Pitch/Yaw print using OrientationEstimator (no SD, no Unity).
// Uses the same complementary filter (accel + gyro + mag) as the main firmware.
// Flash with:  pio test -e seeed_xiao_esp32c3 --filter test_imu_orientation
// Then open the serial monitor at 115200 baud.

#include <Arduino.h>
#include <SPI.h>
#include <math.h>

// Pull in the real OrientationEstimator from src/
#include "../../src/pipeline/OrientationEstimator.h"
#include "../../src/pipeline/OrientationEstimator.cpp"

// ── Pin map (same as main firmware / Pinout.h) ────────────────────────────────
static const int PIN_SPI_SCK  = D6;
static const int PIN_SPI_MISO = D5;
static const int PIN_SPI_MOSI = D4;
static const int PIN_CS_IMU   = D7;

// ── MPU-9250 registers ────────────────────────────────────────────────────────
static const uint8_t REG_WHO_AM_I        = 0x75;
static const uint8_t REG_PWR_MGMT_1      = 0x6B;
static const uint8_t REG_ACCEL_XOUT_H    = 0x3B;
static const uint8_t REG_USER_CTRL       = 0x6A;
static const uint8_t REG_I2C_MST_CTRL    = 0x24;
static const uint8_t REG_I2C_SLV0_ADDR  = 0x25;
static const uint8_t REG_I2C_SLV0_REG   = 0x26;
static const uint8_t REG_I2C_SLV0_CTRL  = 0x27;
static const uint8_t REG_I2C_SLV0_DO    = 0x63;
static const uint8_t REG_EXT_SENS_DATA  = 0x49;

// AK8963 (magnetometer inside MPU-9250)
static const uint8_t AK_ADDR   = 0x0C;
static const uint8_t AK_WIA    = 0x00;
static const uint8_t AK_ST1    = 0x02;
static const uint8_t AK_CNTL1  = 0x0A;

// ── Sample period: match main firmware (80 Hz = 12500 µs) ────────────────────
static const uint32_t kSamplePeriodUs = 12500;

SPIClass SPIbus(FSPI);
OrientationEstimator gOrientation(0.98f);
static uint8_t gAk8963Wia = 0;

// ── Low-level SPI helpers ─────────────────────────────────────────────────────
static void imuWrite(uint8_t reg, uint8_t data)
{
    SPIbus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
    digitalWrite(PIN_CS_IMU, LOW);
    SPIbus.transfer(reg);
    SPIbus.transfer(data);
    digitalWrite(PIN_CS_IMU, HIGH);
    SPIbus.endTransaction();
}

static uint8_t imuRead(uint8_t reg)
{
    SPIbus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
    digitalWrite(PIN_CS_IMU, LOW);
    SPIbus.transfer(reg | 0x80);
    uint8_t val = SPIbus.transfer(0x00);
    digitalWrite(PIN_CS_IMU, HIGH);
    SPIbus.endTransaction();
    return val;
}

static void imuReadBurst(uint8_t reg, uint8_t count, uint8_t *dest)
{
    SPIbus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
    digitalWrite(PIN_CS_IMU, LOW);
    SPIbus.transfer(reg | 0x80);
    for (uint8_t i = 0; i < count; ++i)
    {
        dest[i] = SPIbus.transfer(0x00);
    }
    digitalWrite(PIN_CS_IMU, HIGH);
    SPIbus.endTransaction();
}

// ── AK8963 helpers (via MPU-9250 I2C master) — identical to RawSensor ────────
static bool akWrite(uint8_t reg, uint8_t data)
{
    imuWrite(REG_I2C_SLV0_ADDR, AK_ADDR);
    imuWrite(REG_I2C_SLV0_REG,  reg);
    imuWrite(REG_I2C_SLV0_DO,   data);
    imuWrite(REG_I2C_SLV0_CTRL, 0x81);
    delay(2);
    imuWrite(REG_I2C_SLV0_CTRL, 0x00); // disable slave after write
    return true;
}

static bool akRead(uint8_t reg, uint8_t count, uint8_t *dest)
{
    imuWrite(REG_I2C_SLV0_ADDR, static_cast<uint8_t>(0x80 | AK_ADDR));
    imuWrite(REG_I2C_SLV0_REG,  reg);
    imuWrite(REG_I2C_SLV0_CTRL, static_cast<uint8_t>(0x80 | count));
    delay(2);                                  // same as RawSensor (was 5)
    imuReadBurst(REG_EXT_SENS_DATA, count, dest);
    imuWrite(REG_I2C_SLV0_CTRL, 0x00);        // disable slave after read
    return true;
}

// ── IMU init ──────────────────────────────────────────────────────────────────
static bool initIMU()
{
    // Simple wake-up — no hard reset (same as original working test)
    imuWrite(REG_PWR_MGMT_1, 0x00);
    delay(100);

    uint8_t whoami = imuRead(REG_WHO_AM_I);
    Serial.printf("MPU WHO_AM_I = 0x%02X\n", whoami);
    Serial.flush();
    if (whoami == 0x00 || whoami == 0xFF)
    {
        Serial.println("MPU9250 not responding – check SPI wiring");
        Serial.flush();
        return false;
    }

    // Enable I2C master only (0x20) — no I2C_IF_DIS, same as original test
    imuWrite(REG_USER_CTRL,    0x20);
    imuWrite(REG_I2C_MST_CTRL, 0x0D); // 400 kHz
    delay(10);

    // Verify writes took effect
    uint8_t uctrl   = imuRead(REG_USER_CTRL);
    uint8_t mstctrl = imuRead(REG_I2C_MST_CTRL);
    Serial.printf("USER_CTRL=0x%02X I2C_MST_CTRL=0x%02X\n", uctrl, mstctrl);
    Serial.flush();

    // Read AK8963 WIA — try increasing delays
    uint8_t wia = 0;
    for (int attempt = 1; attempt <= 3 && wia == 0; attempt++)
    {
        imuWrite(REG_I2C_SLV0_ADDR, static_cast<uint8_t>(0x80 | AK_ADDR));
        imuWrite(REG_I2C_SLV0_REG,  AK_WIA);
        imuWrite(REG_I2C_SLV0_CTRL, 0x81);
        uint32_t waitMs = attempt * 20; // 20, 40, 60 ms
        delay(waitMs);
        wia = imuRead(REG_EXT_SENS_DATA);
        uint8_t mstStatus = imuRead(0x36); // I2C_MST_STATUS
        imuWrite(REG_I2C_SLV0_CTRL, 0x00);
        Serial.printf("  WIA attempt %d (%lums): WIA=0x%02X MST_STATUS=0x%02X\n",
                      attempt, (unsigned long)waitMs, wia, mstStatus);
        Serial.flush();
        delay(5);
    }

    gAk8963Wia = wia;
    Serial.printf("AK8963 WIA = 0x%02X\n", wia);
    Serial.flush();

    if (wia == 0x48)
    {
        // Power down AK8963 then start continuous mode 2 (16-bit, 100 Hz)
        imuWrite(REG_I2C_SLV0_ADDR, AK_ADDR);
        imuWrite(REG_I2C_SLV0_REG,  AK_CNTL1);
        imuWrite(REG_I2C_SLV0_CTRL, 0x81);
        imuWrite(REG_I2C_SLV0_DO,   0x00);
        delay(10);

        imuWrite(REG_I2C_SLV0_ADDR, AK_ADDR);
        imuWrite(REG_I2C_SLV0_REG,  AK_CNTL1);
        imuWrite(REG_I2C_SLV0_CTRL, 0x81);
        imuWrite(REG_I2C_SLV0_DO,   0x16);
        delay(10);

        // Auto-fetch ST1+data+ST2 (8 bytes) from AK8963 into EXT_SENS_DATA
        imuWrite(REG_I2C_SLV0_ADDR, static_cast<uint8_t>(0x80 | AK_ADDR));
        imuWrite(REG_I2C_SLV0_REG,  AK_ST1);
        imuWrite(REG_I2C_SLV0_CTRL, 0x88);
        delay(10);

        Serial.println("AK8963 OK – auto-read active");
    }
    else
    {
        Serial.println("AK8963 not found – yaw from gyro only");
    }
    Serial.flush();

    return true;
}

// ── Read one IMU sample ───────────────────────────────────────────────────────
static void readIMU(int16_t &ax, int16_t &ay, int16_t &az,
                    int16_t &gx, int16_t &gy, int16_t &gz,
                    int16_t &mx, int16_t &my, int16_t &mz)
{
    uint8_t raw[14];
    imuReadBurst(REG_ACCEL_XOUT_H, 14, raw);

    ax = static_cast<int16_t>((raw[0]  << 8) | raw[1]);
    ay = static_cast<int16_t>((raw[2]  << 8) | raw[3]);
    az = static_cast<int16_t>((raw[4]  << 8) | raw[5]);
    // raw[6..7] = temp, skip
    gx = static_cast<int16_t>((raw[8]  << 8) | raw[9]);
    gy = static_cast<int16_t>((raw[10] << 8) | raw[11]);
    gz = static_cast<int16_t>((raw[12] << 8) | raw[13]);

    mx = my = mz = 0;

    // Mag data is auto-fetched by the I2C master into EXT_SENS_DATA each cycle
    uint8_t magRaw[8] = {0};
    imuReadBurst(REG_EXT_SENS_DATA, 8, magRaw);

    bool ready    = (magRaw[0] & 0x01) != 0;
    bool overflow = (magRaw[7] & 0x08) != 0;
    if (ready && !overflow)
    {
        mx = static_cast<int16_t>((magRaw[2] << 8) | magRaw[1]);
        my = static_cast<int16_t>((magRaw[4] << 8) | magRaw[3]);
        mz = static_cast<int16_t>((magRaw[6] << 8) | magRaw[5]);
    }
}

// ── Arduino entry points ──────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // never block on serial write if no host connected

    // Wait up to 3 s for a serial host; proceed anyway if none connects
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) { delay(10); }
    delay(100);

    Serial.println("\n=== petBionics orientation debug (RPY stream, no SD) ===");
    Serial.flush();

    pinMode(PIN_CS_IMU, OUTPUT);
    digitalWrite(PIN_CS_IMU, HIGH);
    SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);
    Serial.println("SPI init OK");
    Serial.flush();

    if (!initIMU())
    {
        // Keep printing so the user can see the failure in the monitor
        while (true)
        {
            Serial.println("ERROR: IMU init failed – check wiring");
            Serial.flush();
            delay(1000);
        }
    }

    gOrientation.reset();
    Serial.println("IMU OK – streaming roll,pitch,yaw ...");
    Serial.println("roll_deg,pitch_deg,yaw_deg,ax,ay,az,gx,gy,gz,mx,my,mz");
    Serial.flush();
}

void loop()
{
    static uint32_t lastUs    = micros();
    static uint32_t lastStatMs = 0;

    // Wait until one sample period has elapsed
    while ((micros() - lastUs) < kSamplePeriodUs) { /* busy-wait */ }
    const float dtSeconds = static_cast<float>(micros() - lastUs) / 1000000.0f;
    lastUs = micros();

    int16_t ax, ay, az, gx, gy, gz, mx, my, mz;
    readIMU(ax, ay, az, gx, gy, gz, mx, my, mz);

    const Orientation o = gOrientation.update(ax, ay, az,
                                               gx, gy, gz,
                                               mx, my, mz,
                                               dtSeconds);

    Serial.printf("%.2f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                  o.roll, o.pitch, o.yaw,
                  ax, ay, az,
                  gx, gy, gz,
                  mx, my, mz);

    // Print status line every 2 s so it's visible even after boot
    uint32_t nowMs = millis();
    if (nowMs - lastStatMs >= 2000)
    {
        lastStatMs = nowMs;
        uint8_t uctrl   = imuRead(REG_USER_CTRL);
        uint8_t mststat = imuRead(0x36);
        Serial.printf("# WIA=0x%02X mag_ok=%s mx=%d my=%d mz=%d USER_CTRL=0x%02X MST_STATUS=0x%02X\n",
                      gAk8963Wia,
                      gAk8963Wia == 0x48 ? "YES" : "NO",
                      mx, my, mz,
                      uctrl, mststat);
        Serial.flush();
    }
}
