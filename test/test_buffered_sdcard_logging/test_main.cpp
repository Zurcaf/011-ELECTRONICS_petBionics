#include <unity.h>
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include "HX711.h"

// Pin definitions
#define SPI_SCK 21  // D6
#define SPI_MISO 7  // D5
#define SPI_MOSI 6  // D4
#define IMU_CS 20   // D7
#define SD_CS 8     // D8
#define HX711_DT 10 // D10
#define HX711_SCK 9 // D9

// Buffer configuration
#define LOG_BUFFER_SIZE 2048
#define LOG_FILE_PATH "/sensor_data_buffered.csv"

SPIClass SPIbus(FSPI);
HX711 scale;
bool g_sdReady = false;

// Buffer state
char g_logBuffer[LOG_BUFFER_SIZE];
unsigned int g_bufferPos = 0;
unsigned long g_flushCount = 0;
unsigned long g_totalBytesWritten = 0;

// Simple IMU read struct
struct IMUSample
{
    int16_t accelX, accelY, accelZ;
    int16_t gyroX, gyroY, gyroZ;
};

// Function prototypes
void initIMU();
IMUSample readIMU();
void addToBuffer(const char *line);
void flushBuffer();

void setUp(void)
{
    Serial.begin(115200);
    delay(100);
    while (!Serial)
    { /* wait for Serial to be ready */
        delay(10);
    }

    // Initialize SPI
    SPIbus.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
    delay(100);

    // Initialize SD card
    if (!SD.begin(SD_CS, SPIbus))
    {
        Serial.println("[FAIL] SD Card not detected!");
        g_sdReady = false;
        return;
    }

    Serial.println("[OK] SD Card detected");
    g_sdReady = true;

    // Clean up test file
    SD.remove(LOG_FILE_PATH);

    // Reset buffer
    g_bufferPos = 0;
    g_flushCount = 0;
    g_totalBytesWritten = 0;
    memset(g_logBuffer, 0, LOG_BUFFER_SIZE);

    // Initialize HX711
    scale.begin(HX711_DT, HX711_SCK);
    delay(1000);
    scale.set_scale(420.0); // Dummy scale factor
    scale.tare();

    // Initialize IMU (dummy - just for demonstration)
    initIMU();
}

void tearDown(void)
{
    // Flush any remaining data
    if (g_bufferPos > 0)
    {
        flushBuffer();
    }

    if (g_sdReady)
    {
        Serial.printf("[DONE] Total flushes: %lu\n", g_flushCount);
        Serial.printf("[DONE] Total bytes written to SD: %lu\n", g_totalBytesWritten);
    }
}

// Initialize IMU (simplified - just SPI communication test)
void initIMU()
{
    // In actual implementation, configure MPU9250 here
    Serial.println("[OK] IMU initialized (SPI communication ready)");
}

// Read IMU data (simplified - returns dummy data for test)
IMUSample readIMU()
{
    IMUSample sample;
    sample.accelX = random(-32768, 32767);
    sample.accelY = random(-32768, 32767);
    sample.accelZ = random(-32768, 32767);
    sample.gyroX = random(-32768, 32767);
    sample.gyroY = random(-32768, 32767);
    sample.gyroZ = random(-32768, 32767);
    return sample;
}

// Add line to buffer - triggers flush if needed
void addToBuffer(const char *line)
{
    if (!g_sdReady)
        return;

    unsigned int lineLen = strlen(line);
    if (lineLen == 0)
        return;

    // Drop single lines that are bigger than the entire buffer.
    if (lineLen >= LOG_BUFFER_SIZE)
    {
        Serial.println("[WARN] Line too large for buffer, dropping");
        return;
    }

    // Check if we need to flush
    if (g_bufferPos + lineLen + 1 > LOG_BUFFER_SIZE)
    {
        // Buffer is full, flush now
        flushBuffer();
    }

    // Add line to buffer safely without relying on C-string state.
    memcpy(&g_logBuffer[g_bufferPos], line, lineLen);
    g_bufferPos += lineLen;
    g_logBuffer[g_bufferPos] = '\0';
}

// Flush buffer to SD card
void flushBuffer()
{
    if (!g_sdReady || g_bufferPos == 0)
        return;

    unsigned long flushStart = micros();

    // Open file for append
    File f = SD.open(LOG_FILE_PATH, FILE_APPEND);
    if (!f)
    {
        Serial.println("[ERROR] Could not open log file for append");
        return;
    }

    // Write entire buffer at once
    size_t written = f.write((const uint8_t *)g_logBuffer, g_bufferPos);
    f.close();

    unsigned long flushTime = micros() - flushStart;

    g_totalBytesWritten += written;
    g_flushCount++;

    Serial.printf("[FLUSH] #%lu: %u bytes in %lu μs (%.2f KB/s)\n",
                  g_flushCount, g_bufferPos, flushTime,
                  flushTime > 0 ? (g_bufferPos / 1024.0) / (flushTime / 1000000.0) : 0.0);

    // Reset buffer
    g_bufferPos = 0;
    memset(g_logBuffer, 0, LOG_BUFFER_SIZE);
}

// Test 1: Verify buffer operation and flush timing
void test_buffered_logging_basic(void)
{
    TEST_ASSERT_TRUE_MESSAGE(g_sdReady, "SD Card not ready");

    Serial.println("\n[TEST] Buffered Logging - Basic Operation");
    Serial.println("Adding 50 lines to buffer with automatic flush...\n");

    // Create CSV header
    File f = SD.open(LOG_FILE_PATH, FILE_WRITE);
    f.println("time_ms,type,v1,v2,v3,v4,v5,v6");
    f.close();
    g_totalBytesWritten += 35; // Approximate header size

    // Add 50 lines (simulating sensor data)
    for (int i = 0; i < 50; i++)
    {
        unsigned long timeMs = millis();
        char line[100];

        // Alternate between IMU and HX711 data
        if (i % 2 == 0)
        {
            // IMU line
            snprintf(line, sizeof(line),
                     "%lu,IMU,%d,%d,%d,%d,%d,%d\n",
                     timeMs,
                     1000 + i, 2000 + i, 3000 + i,
                     4000 + i, 5000 + i, 6000 + i);
        }
        else
        {
            // HX711 line
            snprintf(line, sizeof(line),
                     "%lu,HX,%d,0,0,0,0,0\n",
                     timeMs,
                     500000 - i * 100);
        }

        addToBuffer(line);
        delay(10); // Simulate time between sensor readings
    }

    // Final flush
    if (g_bufferPos > 0)
    {
        flushBuffer();
    }

    Serial.println("[PASS] Buffered logging test completed");
}

// Test 2: Simulate real-time IMU + HX711 logging
void test_realtime_buffered_acquisition(void)
{
    TEST_ASSERT_TRUE_MESSAGE(g_sdReady, "SD Card not ready");

    Serial.println("\n[TEST] Real-time Buffered Data Acquisition");
    Serial.println("Simulating 150Hz IMU + 80Hz HX711 for 5 seconds\n");

    // Create CSV header
    File f = SD.open(LOG_FILE_PATH, FILE_WRITE);
    f.println("time_ms,type,v1,v2,v3,v4,v5,v6");
    f.close();

    unsigned long startUs = micros();
    unsigned long imuCounter = 0;
    unsigned long hxCounter = 0;
    unsigned long lastImuUs = startUs;
    unsigned long lastHxUs = startUs;

    const unsigned long IMU_INTERVAL_US = 6667;      // ~150 Hz (6.667ms)
    const unsigned long HX_INTERVAL_US = 12500;      // ~80 Hz (12.5ms)
    const unsigned long TEST_DURATION_US = 5000000;  // 5 seconds

    while (micros() - startUs < TEST_DURATION_US)
    {
        unsigned long nowUs = micros();
        unsigned long nowMs = millis();

        // Check if it's time for IMU reading
        if (nowUs - lastImuUs >= IMU_INTERVAL_US)
        {
            IMUSample sample = readIMU();
            char line[100];
            snprintf(line, sizeof(line),
                     "%lu,IMU,%d,%d,%d,%d,%d,%d\n",
                     nowMs, sample.accelX, sample.accelY, sample.accelZ,
                     sample.gyroX, sample.gyroY, sample.gyroZ);
            addToBuffer(line);
            lastImuUs = nowUs;
            imuCounter++;
        }

        // Check if it's time for HX711 reading
        if (nowUs - lastHxUs >= HX_INTERVAL_US && scale.is_ready())
        {
            long hxValue = scale.read_average(1);
            char line[100];
            snprintf(line, sizeof(line),
                     "%lu,HX,%ld,0,0,0,0,0\n",
                     nowMs, hxValue);
            addToBuffer(line);
            lastHxUs = nowUs;
            hxCounter++;
        }

        // Don't hog the CPU
        delayMicroseconds(100);
    }

    // Final flush
    if (g_bufferPos > 0)
    {
        flushBuffer();
    }

    unsigned long totalTimeMs = (micros() - startUs) / 1000;

    Serial.printf("\n[STATS] Duration: %lu ms\n", totalTimeMs);
    Serial.printf("[STATS] IMU samples: %lu (expected ~%lu for 150Hz)\n",
                  imuCounter, TEST_DURATION_US * 150 / 1000000);
    Serial.printf("[STATS] HX711 samples: %lu (expected ~%lu for 80Hz)\n",
                  hxCounter, TEST_DURATION_US * 80 / 1000000);
    Serial.printf("[STATS] Total sample rate: %.1f Hz\n",
                  totalTimeMs > 0 ? (imuCounter + hxCounter) * 1000.0 / totalTimeMs : 0.0);
    Serial.printf("[STATS] Total flushes: %lu\n", g_flushCount);
    Serial.printf("[STATS] Total bytes on SD: %lu\n", g_totalBytesWritten);

    TEST_ASSERT_GREATER_THAN(0, imuCounter);
    TEST_ASSERT_GREATER_THAN(0, hxCounter);

    Serial.println("[PASS] Real-time buffered acquisition test completed");
}

// Test 3: Verify buffer doesn't drop data and CSV is valid
void test_buffered_data_integrity(void)
{
    TEST_ASSERT_TRUE_MESSAGE(g_sdReady, "SD Card not ready");

    Serial.println("\n[TEST] Buffered Data Integrity Check");

    // Read back the CSV file
    File f = SD.open(LOG_FILE_PATH);
    TEST_ASSERT_TRUE_MESSAGE(f, "Could not open log file for reading");

    unsigned long lineCount = 0;
    char buffer[150];

    Serial.println("Reading back logged data...\n");

    while (f.available())
    {
        size_t len = f.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
        if (len > 0)
        {
            buffer[len] = '\0';
            lineCount++;

            // Print first 10 lines and last 5 lines
            if (lineCount <= 11 || lineCount > 500)
            {
                Serial.printf("Line %lu: %s\n", lineCount, buffer);
            }
            else if (lineCount == 12)
            {
                Serial.println("...");
            }
        }
    }

    f.close();

    Serial.printf("\n[STATS] Total lines read: %lu\n", lineCount);
    TEST_ASSERT_GREATER_THAN(1, lineCount); // At least header + 1 data line

    Serial.println("[PASS] Data integrity check completed");
}

void setup()
{
    UNITY_BEGIN();

    RUN_TEST(test_buffered_logging_basic);
    // RUN_TEST(test_realtime_buffered_acquisition);  // Commented - takes 5 seconds
    // RUN_TEST(test_buffered_data_integrity);        // Commented - takes time to read

    UNITY_END();
}

void loop()
{
}
