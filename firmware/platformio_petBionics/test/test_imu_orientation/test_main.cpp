#include <Arduino.h>
#include <SPI.h>
#include <unity.h>
#include <math.h>

// Pin map aligned with the other tests
static const int PIN_SPI_SCK = D6;
static const int PIN_SPI_MISO = D5;
static const int PIN_SPI_MOSI = D4;
static const int PIN_CS_IMU = D7;

// MPU9250 registers
static const uint8_t WHO_AM_I = 0x75;
static const uint8_t PWR_MGMT_1 = 0x6B;
static const uint8_t ACCEL_XOUT_H = 0x3B;
static const uint8_t USER_CTRL = 0x6A;
static const uint8_t I2C_MST_CTRL = 0x24;
static const uint8_t I2C_SLV0_ADDR = 0x25;
static const uint8_t I2C_SLV0_REG = 0x26;
static const uint8_t I2C_SLV0_CTRL = 0x27;
static const uint8_t EXT_SENS_DATA_00 = 0x49;

// AK8963 (inside MPU9250)
static const uint8_t AK8963_ADDR = 0x0C;
static const uint8_t AK8963_WIA = 0x00;
static const uint8_t AK8963_ST1 = 0x02;
static const uint8_t AK8963_CNTL1 = 0x0A;

SPIClass SPIbus(FSPI);

struct IMUSample
{
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
    int16_t mx;
    int16_t my;
    int16_t mz;
};

void imuWriteRegister(uint8_t reg, uint8_t data)
{
    SPIbus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
    digitalWrite(PIN_CS_IMU, LOW);
    SPIbus.transfer(reg);
    SPIbus.transfer(data);
    digitalWrite(PIN_CS_IMU, HIGH);
    SPIbus.endTransaction();
}

uint8_t imuReadRegister(uint8_t reg)
{
    SPIbus.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
    digitalWrite(PIN_CS_IMU, LOW);
    SPIbus.transfer(reg | 0x80);
    uint8_t val = SPIbus.transfer(0x00);
    digitalWrite(PIN_CS_IMU, HIGH);
    SPIbus.endTransaction();
    return val;
}

void imuReadBytes(uint8_t reg, uint8_t count, uint8_t *dest)
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

bool initMagnetometer()
{
    imuWriteRegister(USER_CTRL, 0x20);
    imuWriteRegister(I2C_MST_CTRL, 0x0D);
    delay(10);

    imuWriteRegister(I2C_SLV0_ADDR, static_cast<uint8_t>(0x80 | AK8963_ADDR));
    imuWriteRegister(I2C_SLV0_REG, AK8963_WIA);
    imuWriteRegister(I2C_SLV0_CTRL, 0x81);
    delay(5);

    uint8_t wia = imuReadRegister(EXT_SENS_DATA_00);
    Serial.printf("AK8963 WIA = 0x%02X\n", wia);
    if (wia != 0x48)
    {
        return false;
    }

    // Power down then continuous mode 2 (16-bit, 100 Hz).
    imuWriteRegister(I2C_SLV0_ADDR, AK8963_ADDR);
    imuWriteRegister(I2C_SLV0_REG, AK8963_CNTL1);
    imuWriteRegister(I2C_SLV0_CTRL, 0x81);
    imuWriteRegister(0x63, 0x00);
    delay(10);

    imuWriteRegister(I2C_SLV0_ADDR, AK8963_ADDR);
    imuWriteRegister(I2C_SLV0_REG, AK8963_CNTL1);
    imuWriteRegister(I2C_SLV0_CTRL, 0x81);
    imuWriteRegister(0x63, 0x16);
    delay(10);

    return true;
}

void readIMU(IMUSample &s)
{
    uint8_t raw[14];
    imuReadBytes(ACCEL_XOUT_H, 14, raw);

    s.ax = static_cast<int16_t>((raw[0] << 8) | raw[1]);
    s.ay = static_cast<int16_t>((raw[2] << 8) | raw[3]);
    s.az = static_cast<int16_t>((raw[4] << 8) | raw[5]);
    s.gx = static_cast<int16_t>((raw[8] << 8) | raw[9]);
    s.gy = static_cast<int16_t>((raw[10] << 8) | raw[11]);
    s.gz = static_cast<int16_t>((raw[12] << 8) | raw[13]);

    s.mx = s.my = s.mz = 0;

    imuWriteRegister(I2C_SLV0_ADDR, static_cast<uint8_t>(0x80 | AK8963_ADDR));
    imuWriteRegister(I2C_SLV0_REG, AK8963_ST1);
    imuWriteRegister(I2C_SLV0_CTRL, 0x88);
    delay(3);

    uint8_t magRaw[8];
    imuReadBytes(EXT_SENS_DATA_00, 8, magRaw);

    bool ready = (magRaw[0] & 0x01) != 0;
    bool overflow = (magRaw[7] & 0x08) != 0;
    if (ready && !overflow)
    {
        s.mx = static_cast<int16_t>((magRaw[2] << 8) | magRaw[1]);
        s.my = static_cast<int16_t>((magRaw[4] << 8) | magRaw[3]);
        s.mz = static_cast<int16_t>((magRaw[6] << 8) | magRaw[5]);
    }
}

void computeRollPitchYawDeg(const IMUSample &s, float &rollDeg, float &pitchDeg, float &yawDeg)
{
    const float ax = static_cast<float>(s.ax);
    const float ay = static_cast<float>(s.ay);
    const float az = static_cast<float>(s.az);
    const float mx = static_cast<float>(s.mx);
    const float my = static_cast<float>(s.my);
    const float mz = static_cast<float>(s.mz);

    const float roll = atan2f(ay, az);
    const float pitch = atan2f(-ax, sqrtf((ay * ay) + (az * az)));

    const float mxComp = mx * cosf(pitch) + mz * sinf(pitch);
    const float myComp = mx * sinf(roll) * sinf(pitch) + my * cosf(roll) - mz * sinf(roll) * cosf(pitch);
    float yaw = atan2f(-myComp, mxComp);

    const float radToDeg = 57.2957795f;
    rollDeg = roll * radToDeg;
    pitchDeg = pitch * radToDeg;
    yawDeg = yaw * radToDeg;
    if (yawDeg < 0.0f)
    {
        yawDeg += 360.0f;
    }
}

void test_mpu9250_orientation_stream()
{
    Serial.println("\n[TEST] MPU9250 orientation (roll/pitch/yaw)");

    const uint8_t whoami = imuReadRegister(WHO_AM_I);
    Serial.printf("MPU WHO_AM_I = 0x%02X\n", whoami);
    TEST_ASSERT_TRUE_MESSAGE(whoami != 0x00 && whoami != 0xFF, "IMU communication failed");

    const bool magReady = initMagnetometer();
    TEST_ASSERT_TRUE_MESSAGE(magReady, "AK8963 not detected");

    const int sampleCount = 80;
    uint16_t validMagSamples = 0;
    float lastRoll = 0.0f;
    float lastPitch = 0.0f;
    float lastYaw = 0.0f;

    IMUSample s = {0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < sampleCount; ++i)
    {
        readIMU(s);
        computeRollPitchYawDeg(s, lastRoll, lastPitch, lastYaw);

        if (s.mx != 0 || s.my != 0 || s.mz != 0)
        {
            validMagSamples++;
        }

        Serial.printf("[%02d] A=%d,%d,%d G=%d,%d,%d M=%d,%d,%d | RPY=%.2f,%.2f,%.2f\n",
                      i,
                      s.ax, s.ay, s.az,
                      s.gx, s.gy, s.gz,
                      s.mx, s.my, s.mz,
                      lastRoll, lastPitch, lastYaw);

        TEST_ASSERT_TRUE_MESSAGE(!isnan(lastRoll) && !isnan(lastPitch) && !isnan(lastYaw), "RPY produced NaN");
        delay(100);
    }

    Serial.printf("Samples=%d valid_mag=%u last_rpy=%.2f,%.2f,%.2f\n",
                  sampleCount,
                  static_cast<unsigned>(validMagSamples),
                  lastRoll,
                  lastPitch,
                  lastYaw);

    TEST_ASSERT_TRUE_MESSAGE(validMagSamples > 10, "Too few valid magnetometer samples");
}

void setup()
{
    delay(500);
    Serial.begin(115200);
    while (!Serial)
    {
        delay(10);
    }

    Serial.println("\n=== petBionics MPU9250 orientation test ===");
    Serial.printf("Pins SPI SCK=%d MISO=%d MOSI=%d CS_IMU=%d\n",
                  PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);

    pinMode(PIN_CS_IMU, OUTPUT);
    digitalWrite(PIN_CS_IMU, HIGH);

    SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);
    imuWriteRegister(PWR_MGMT_1, 0x00);
    delay(100);

    UNITY_BEGIN();
    RUN_TEST(test_mpu9250_orientation_stream);
    UNITY_END();
}

void loop()
{
}
