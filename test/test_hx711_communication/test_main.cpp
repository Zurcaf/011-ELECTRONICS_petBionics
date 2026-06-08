#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <HX711.h>
#include <unity.h>

// Pin map aligned with integrated Arduino sketches (main/main1):
// SPI: SCK=D6, MISO=D5, MOSI=D4
// IMU CS=D7, SD CS=D8
// HX711: DT=D10, SCK=D9
static const int PIN_SPI_SCK = D6;
static const int PIN_SPI_MISO = D5;
static const int PIN_SPI_MOSI = D4;
static const int PIN_CS_IMU = D7;
static const int PIN_CS_SD = D8;
static const int HX711_DOUT_PIN = D10;
static const int HX711_SCK_PIN = D9;

static const uint8_t WHO_AM_I = 0x75;
static const uint8_t PWR_MGMT_1 = 0x6B;
static const uint8_t ACCEL_XOUT_H = 0x3B;
static const uint8_t USER_CTRL = 0x6A;
static const uint8_t I2C_MST_CTRL = 0x24;
static const uint8_t I2C_SLV0_ADDR = 0x25;
static const uint8_t I2C_SLV0_REG = 0x26;
static const uint8_t I2C_SLV0_CTRL = 0x27;
static const uint8_t I2C_SLV4_ADDR = 0x31;
static const uint8_t I2C_SLV4_REG = 0x32;
static const uint8_t I2C_SLV4_DO = 0x33;
static const uint8_t I2C_SLV4_CTRL = 0x34;
static const uint8_t I2C_SLV4_DI = 0x35;
static const uint8_t EXT_SENS_DATA_00 = 0x49;

static const uint8_t AK8963_ADDR = 0x0C;
static const uint8_t AK8963_WIA = 0x00;
static const uint8_t AK8963_ST1 = 0x02;
static const uint8_t AK8963_CNTL1 = 0x0A;
static const char *LOG_FILE_PATH = "/sensor_log.csv";
static const char *COMPARE_FILE_PATH = "/sensor_compare.csv";

SPIClass SPIbus(FSPI);

HX711 scale;
bool g_sdReady = false;
bool g_magReady = false;

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
    for (uint8_t i = 0; i < count; i++)
    {
        dest[i] = SPIbus.transfer(0x00);
    }
    digitalWrite(PIN_CS_IMU, HIGH);
    SPIbus.endTransaction();
}

void appendLogLine(const String &line)
{
    if (!g_sdReady)
    {
        return;
    }

    File f = SD.open(LOG_FILE_PATH, FILE_APPEND);
    if (!f)
    {
        Serial.println("[WARN] Could not open sensor_log.csv for append");
        return;
    }

    f.println(line);
    f.close();
}

bool ak8963WriteRegister(uint8_t reg, uint8_t value)
{
    imuWriteRegister(I2C_SLV4_ADDR, AK8963_ADDR);
    imuWriteRegister(I2C_SLV4_REG, reg);
    imuWriteRegister(I2C_SLV4_DO, value);
    imuWriteRegister(I2C_SLV4_CTRL, 0x80);
    delay(10);
    return true;
}

bool ak8963ReadRegister(uint8_t reg, uint8_t &value)
{
    imuWriteRegister(I2C_SLV4_ADDR, 0x80 | AK8963_ADDR);
    imuWriteRegister(I2C_SLV4_REG, reg);
    imuWriteRegister(I2C_SLV4_CTRL, 0x80);
    delay(10);
    value = imuReadRegister(I2C_SLV4_DI);
    return true;
}

void initMagnetometer()
{
    imuWriteRegister(USER_CTRL, 0x20);    // Enable MPU I2C master mode.
    imuWriteRegister(I2C_MST_CTRL, 0x0D); // I2C master clock.
    delay(10);

    uint8_t wia = 0;
    ak8963ReadRegister(AK8963_WIA, wia);
    Serial.printf("AK8963 WIA = 0x%02X\n", wia);

    // Power down then continuous measurement mode 2 (16-bit, 100 Hz).
    ak8963WriteRegister(AK8963_CNTL1, 0x00);
    delay(10);
    ak8963WriteRegister(AK8963_CNTL1, 0x16);
    delay(10);

    g_magReady = (wia == 0x48);
}

bool readIMUSample(IMUSample &s)
{
    uint8_t raw[14];
    imuReadBytes(ACCEL_XOUT_H, 14, raw);

    s.ax = (raw[0] << 8) | raw[1];
    s.ay = (raw[2] << 8) | raw[3];
    s.az = (raw[4] << 8) | raw[5];
    s.gx = (raw[8] << 8) | raw[9];
    s.gy = (raw[10] << 8) | raw[11];
    s.gz = (raw[12] << 8) | raw[13];

    s.mx = 0;
    s.my = 0;
    s.mz = 0;

    if (g_magReady)
    {
        // Read 7 bytes from AK8963 starting at ST1 via MPU external sensor registers.
        imuWriteRegister(I2C_SLV0_ADDR, 0x80 | AK8963_ADDR);
        imuWriteRegister(I2C_SLV0_REG, AK8963_ST1);
        imuWriteRegister(I2C_SLV0_CTRL, 0x87);
        delay(10);

        uint8_t st1 = imuReadRegister(EXT_SENS_DATA_00 + 0);
        uint8_t mxl = imuReadRegister(EXT_SENS_DATA_00 + 1);
        uint8_t mxh = imuReadRegister(EXT_SENS_DATA_00 + 2);
        uint8_t myl = imuReadRegister(EXT_SENS_DATA_00 + 3);
        uint8_t myh = imuReadRegister(EXT_SENS_DATA_00 + 4);
        uint8_t mzl = imuReadRegister(EXT_SENS_DATA_00 + 5);
        uint8_t mzh = imuReadRegister(EXT_SENS_DATA_00 + 6);

        if (st1 & 0x01)
        {
            s.mx = (int16_t)((mxh << 8) | mxl);
            s.my = (int16_t)((myh << 8) | myl);
            s.mz = (int16_t)((mzh << 8) | mzl);
        }
    }

    return true;
}

void appendCompareLine(const String &line)
{
    if (!g_sdReady)
    {
        return;
    }

    File f = SD.open(COMPARE_FILE_PATH, FILE_APPEND);
    if (!f)
    {
        Serial.println("[WARN] Could not open sensor_compare.csv for append");
        return;
    }

    f.println(line);
    f.close();
}

void test_imu_communication()
{
    Serial.println("\n[TEST] IMU communication");
    imuWriteRegister(PWR_MGMT_1, 0x00);
    delay(100);
    initMagnetometer();

    uint8_t whoami = imuReadRegister(WHO_AM_I);
    Serial.printf("IMU WHO_AM_I = 0x%02X\n", whoami);
    appendLogLine(String(millis()) + ",IMU_WHOAMI," + String(whoami, HEX));

    IMUSample s;
    readIMUSample(s);

    Serial.printf("IMU ACCEL=%d,%d,%d | GYRO=%d,%d,%d | MAG=%d,%d,%d\n",
                  s.ax, s.ay, s.az, s.gx, s.gy, s.gz, s.mx, s.my, s.mz);
    appendLogLine(String(millis()) + ",IMU," + String(s.ax) + "," + String(s.ay) + "," +
                  String(s.az) + "," + String(s.gx) + "," + String(s.gy) + "," + String(s.gz));
    appendLogLine(String(millis()) + ",MAG," + String(s.mx) + "," + String(s.my) + "," +
                  String(s.mz));

    // Typical MPU responses are 0x68/0x70/0x71/0x73 depending on variant.
    bool likelyValid = (whoami != 0x00) && (whoami != 0xFF);
    TEST_ASSERT_TRUE_MESSAGE(likelyValid, "IMU WHO_AM_I invalid (0x00/0xFF). Check SPI wiring/CS.");
}

void test_sdcard_read_write()
{
    Serial.println("\n[TEST] SD card read/write");

    bool sdOk = SD.begin(PIN_CS_SD, SPIbus);
    g_sdReady = sdOk;
    Serial.printf("SD.begin -> %s\n", sdOk ? "OK" : "FAIL");
    TEST_ASSERT_TRUE_MESSAGE(sdOk, "SD.begin failed. Check SD wiring/CS.");

    SD.remove(LOG_FILE_PATH);
    File log = SD.open(LOG_FILE_PATH, FILE_WRITE);
    TEST_ASSERT_TRUE_MESSAGE(log, "Could not create sensor_log.csv.");
    log.println("time_ms,type,v1,v2,v3,v4,v5,v6");
    log.close();
    Serial.printf("Log file ready: %s\n", LOG_FILE_PATH);

    SD.remove(COMPARE_FILE_PATH);
    File compareLog = SD.open(COMPARE_FILE_PATH, FILE_WRITE);
    TEST_ASSERT_TRUE_MESSAGE(compareLog, "Could not create sensor_compare.csv.");
    compareLog.println("sample,time_ms,ax,ay,az,gx,gy,gz,mx,my,mz,hx_raw");
    compareLog.close();
    Serial.printf("Compare file ready: %s\n", COMPARE_FILE_PATH);

    const char *path = "/pio_test.txt";
    const char *payload = "petBionics SD test";

    File f = SD.open(path, FILE_WRITE);
    TEST_ASSERT_TRUE_MESSAGE(f, "Could not open SD file for write.");
    f.println(payload);
    f.close();

    f = SD.open(path, FILE_READ);
    TEST_ASSERT_TRUE_MESSAGE(f, "Could not open SD file for read.");

    String line = f.readStringUntil('\n');
    line.trim();
    f.close();

    Serial.printf("SD read line: %s\n", line.c_str());
    TEST_ASSERT_TRUE_MESSAGE(line == payload, "SD read content mismatch.");
}

void test_hx711_is_detected()
{
    Serial.println("\n[TEST] HX711 detect");
    bool ready = scale.wait_ready_timeout(3000);
    Serial.printf("HX711 ready -> %s\n", ready ? "YES" : "NO");
    appendLogLine(String(millis()) + ",HX711_READY," + String(ready ? 1 : 0));
    TEST_ASSERT_TRUE_MESSAGE(ready, "HX711 not detected. Check power and wiring.");
}

void test_hx711_raw_reads_are_available_and_in_range()
{
    Serial.println("\n[TEST] HX711 raw reads");
    const int sampleCount = 8;
    long minValue = 0;
    long maxValue = 0;

    for (int i = 0; i < sampleCount; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(scale.wait_ready_timeout(1000), "HX711 not ready for read.");

        long raw = scale.read();
        Serial.printf("HX711 raw[%d] = %ld\n", i, raw);
        appendLogLine(String(millis()) + ",HX711_RAW," + String(i) + "," + String(raw));

        // HX711 raw output is a signed 24-bit value.
        TEST_ASSERT_TRUE_MESSAGE(raw >= -8388608L && raw <= 8388607L,
                                 "HX711 raw value out of 24-bit range.");

        if (i == 0)
        {
            minValue = raw;
            maxValue = raw;
        }
        else
        {
            if (raw < minValue)
            {
                minValue = raw;
            }
            if (raw > maxValue)
            {
                maxValue = raw;
            }
        }
    }

    if (minValue == maxValue)
    {
        Serial.println("[WARN] All HX711 samples were identical. Communication is OK, but check load cell wiring/mechanics.");
    }
}

void test_compare_imu_and_hx711_samples()
{
    Serial.println("\n[TEST] Compare IMU vs HX711 (synchronized samples)");

    const int sampleCount = 20;
    for (int i = 0; i < sampleCount; i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(scale.wait_ready_timeout(1000), "HX711 not ready during compare sampling.");

        IMUSample s;
        readIMUSample(s);
        long hx = scale.read();

        String line = String(i) + "," + String(millis()) + "," + String(s.ax) + "," +
                      String(s.ay) + "," + String(s.az) + "," + String(s.gx) + "," +
                      String(s.gy) + "," + String(s.gz) + "," + String(s.mx) + "," +
                      String(s.my) + "," + String(s.mz) + "," + String(hx);

        Serial.println("COMPARE," + line);
        appendCompareLine(line);

        delay(100);
    }
}

void setup()
{
    delay(1000);
    Serial.begin(115200);
    while (!Serial)
    { /* wait for Serial to be ready */
        delay(10);
    }

    Serial.println("\n=== petBionics integrated hardware tests ===");
    Serial.println("Pins:");
    Serial.printf("SPI SCK=%d MISO=%d MOSI=%d | CS_IMU=%d CS_SD=%d\n", PIN_SPI_SCK,
                  PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU, PIN_CS_SD);
    Serial.printf("HX711 DT=%d SCK=%d\n", HX711_DOUT_PIN, HX711_SCK_PIN);

    pinMode(PIN_CS_IMU, OUTPUT);
    pinMode(PIN_CS_SD, OUTPUT);
    digitalWrite(PIN_CS_IMU, HIGH);
    digitalWrite(PIN_CS_SD, HIGH);

    SPIbus.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, PIN_CS_IMU);

    scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);

    UNITY_BEGIN();
    RUN_TEST(test_sdcard_read_write);
    RUN_TEST(test_imu_communication);
    RUN_TEST(test_hx711_is_detected);
    RUN_TEST(test_hx711_raw_reads_are_available_and_in_range);
    RUN_TEST(test_compare_imu_and_hx711_samples);
    UNITY_END();
}

void loop()
{
}
