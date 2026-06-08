#include <Arduino.h>
#include <SPI.h>
#include <unity.h>

// Pin map aligned with integrated Arduino sketches (main/main1):
// SPI: SCK=D6, MISO=D5, MOSI=D4
// IMU CS=D7
static const int PIN_SPI_SCK = D6;
static const int PIN_SPI_MISO = D5;
static const int PIN_SPI_MOSI = D4;
static const int PIN_CS_IMU = D7;

static const uint8_t WHO_AM_I = 0x75;
static const uint8_t PWR_MGMT_1 = 0x6B;
static const uint8_t ACCEL_XOUT_H = 0x3B;

SPIClass SPIbus(FSPI);

struct IMUSample
{
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
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
    for (uint8_t i = 0; i < count; i++)
    {
        dest[i] = SPIbus.transfer(0x00);
    }
    digitalWrite(PIN_CS_IMU, HIGH);
    SPIbus.endTransaction();
}

void readIMU(IMUSample &s)
{
    uint8_t raw[14];
    imuReadBytes(ACCEL_XOUT_H, 14, raw);
    s.ax = (raw[0] << 8) | raw[1];
    s.ay = (raw[2] << 8) | raw[3];
    s.az = (raw[4] << 8) | raw[5];
    s.gx = (raw[8] << 8) | raw[9];
    s.gy = (raw[10] << 8) | raw[11];
    s.gz = (raw[12] << 8) | raw[13];
}

void test_max_sampling_rate_imu_only()
{
    Serial.println("\n[TEST] Max sampling rate IMU only");

    uint8_t whoami = imuReadRegister(WHO_AM_I);
    Serial.printf("IMU WHO_AM_I = 0x%02X\n", whoami);
    TEST_ASSERT_TRUE_MESSAGE(whoami != 0x00 && whoami != 0xFF, "IMU communication failed.");

    const int sampleCount = 400;
    unsigned long prevUs = 0;
    unsigned long minDtUs = 0xFFFFFFFF;
    unsigned long maxDtUs = 0;
    unsigned long sumDtUs = 0;

    IMUSample lastImu = {0, 0, 0, 0, 0, 0};

    Serial.println("Collecting IMU-only samples with minimum interval...");
    for (int i = 0; i < sampleCount; i++)
    {
        unsigned long nowUs = micros();
        readIMU(lastImu);

        if (i > 0)
        {
            unsigned long dtUs = nowUs - prevUs;
            sumDtUs += dtUs;
            if (dtUs < minDtUs)
                minDtUs = dtUs;
            if (dtUs > maxDtUs)
                maxDtUs = dtUs;
        }
        prevUs = nowUs;
    }

    float avgDtUs = (sampleCount > 1) ? (float)sumDtUs / (float)(sampleCount - 1) : 0.0f;
    float avgHz = (avgDtUs > 0.0f) ? (1000000.0f / avgDtUs) : 0.0f;
    float minHz = (maxDtUs > 0) ? (1000000.0f / (float)maxDtUs) : 0.0f;
    float maxHz = (minDtUs > 0) ? (1000000.0f / (float)minDtUs) : 0.0f;

    Serial.printf("Samples: %d\n", sampleCount);
    Serial.printf("dt_us min/avg/max = %lu / %.2f / %lu\n", minDtUs, avgDtUs, maxDtUs);
    Serial.printf("rate_hz min/avg/max = %.2f / %.2f / %.2f\n", minHz, avgHz, maxHz);
    Serial.printf("Last IMU A=%d,%d,%d G=%d,%d,%d\n",
                  lastImu.ax, lastImu.ay, lastImu.az,
                  lastImu.gx, lastImu.gy, lastImu.gz);

    TEST_ASSERT_TRUE_MESSAGE(sampleCount > 1, "Not enough samples.");
    TEST_ASSERT_TRUE_MESSAGE(avgHz > 1.0f, "Average IMU sample rate too low.");
}

void setup()
{
    delay(500);
    Serial.begin(115200);
    while (!Serial)
    {
        delay(10);
    }

    Serial.println("\n=== petBionics high-rate IMU-only test ===");
    Serial.printf("Pins SPI SCK=%d MISO=%d MOSI=%d CS_IMU=%d\n",
                  PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);

    pinMode(PIN_CS_IMU, OUTPUT);
    digitalWrite(PIN_CS_IMU, HIGH);

    SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);
    imuWriteRegister(PWR_MGMT_1, 0x00);
    delay(100);

    UNITY_BEGIN();
    RUN_TEST(test_max_sampling_rate_imu_only);
    UNITY_END();
}

void loop()
{
}
