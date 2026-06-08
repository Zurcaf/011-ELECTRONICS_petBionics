#include <unity.h>
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// Pin definitions for ESP32-C3
#define SPI_SCK 21 // D6
#define SPI_MISO 7 // D5
#define SPI_MOSI 6 // D4
#define SD_CS 8    // D8

SPIClass SPIbus(FSPI);
bool g_sdReady = false;

void setUp(void)
{
    Serial.begin(115200);
    delay(100);

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

    // Clean up test files
    SD.remove("/sdcard_write_speed_test.bin");
}

void tearDown(void)
{
    // Cleanup
    if (g_sdReady)
    {
        SD.remove("/sdcard_write_speed_test.bin");
    }
}

// Test 1: Write speed with different block sizes
void test_sdcard_write_speed_different_block_sizes(void)
{
    TEST_ASSERT_TRUE_MESSAGE(g_sdReady, "SD Card not ready");

    Serial.println("\n[TEST] SD Card Write Speed - Different Block Sizes");
    Serial.println("Size(bytes)\tTime(ms)\tSpeed(KB/s)");

    // Test different block sizes: 128B, 256B, 512B, 1KB, 2KB, 4KB
    unsigned int blockSizes[] = {128, 256, 512, 1024, 2048, 4096};
    unsigned int numBlocks = 100; // Write 100 blocks of each size

    for (int i = 0; i < 6; i++)
    {
        unsigned int blockSize = blockSizes[i];
        unsigned long totalSize = blockSize * numBlocks;

        // Create test buffer
        char *buffer = (char *)malloc(blockSize);
        if (!buffer)
        {
            Serial.println("[ERROR] Memory allocation failed");
            return;
        }
        memset(buffer, 0xAA, blockSize);

        // Delete previous test file
        SD.remove("/sdcard_write_speed_test.bin");

        // Open file for writing
        File f = SD.open("/sdcard_write_speed_test.bin", FILE_WRITE);
        TEST_ASSERT_TRUE_MESSAGE(f, "Could not open test file");

        // Measure write time
        unsigned long startUs = micros();

        for (unsigned int j = 0; j < numBlocks; j++)
        {
            f.write((uint8_t *)buffer, blockSize);
        }

        f.flush(); // Flush before close
        unsigned long flushUs = micros() - startUs;

        unsigned long closeStart = micros();
        f.close();
        unsigned long closeTime = micros() - closeStart;

        unsigned long totalUs = micros() - startUs;

        // Calculate speed
        float speedKBs = (totalSize / 1024.0) / (totalUs / 1000000.0);

        Serial.printf("%u\t\t%lu\t%.2f\n", blockSize, totalUs / 1000, speedKBs);

        free(buffer);

        delay(50); // Small delay between tests
    }

    Serial.println("[PASS] Write speed test completed");
}

// Test 2: Measure file open/close overhead
void test_sdcard_open_close_overhead(void)
{
    TEST_ASSERT_TRUE_MESSAGE(g_sdReady, "SD Card not ready");

    Serial.println("\n[TEST] SD Card Open/Close Overhead");
    Serial.println("Operation\t\t\tTime(μs)");

    // Test 1: Open only
    SD.remove("/overhead_test.txt");
    unsigned long start = micros();
    File f = SD.open("/overhead_test.txt", FILE_WRITE);
    unsigned long openTime = micros() - start;
    f.close();

    Serial.printf("Open file\t\t\t%lu\n", openTime);

    // Test 2: Close only (file already open)
    f = SD.open("/overhead_test.txt", FILE_WRITE);
    start = micros();
    f.close();
    unsigned long closeTime = micros() - start;

    Serial.printf("Close file\t\t\t%lu\n", closeTime);

    // Test 3: Open + write 1 byte + close
    SD.remove("/overhead_test.txt");
    start = micros();
    f = SD.open("/overhead_test.txt", FILE_WRITE);
    f.write('A');
    f.close();
    unsigned long singleWriteTime = micros() - start;

    Serial.printf("Open + write 1 byte + close\t%lu\n", singleWriteTime);

    // Test 4: Open + write 100 bytes + close
    SD.remove("/overhead_test.txt");
    char buffer[100];
    memset(buffer, 'B', 100);
    start = micros();
    f = SD.open("/overhead_test.txt", FILE_WRITE);
    f.write((uint8_t *)buffer, 100);
    f.close();
    unsigned long bulk100WriteTime = micros() - start;

    Serial.printf("Open + write 100 bytes + close\t%lu\n", bulk100WriteTime);

    Serial.println("[PASS] Open/close overhead test completed");
}

// Test 3: Flush timing with different buffer sizes
void test_sdcard_flush_timing_different_sizes(void)
{
    TEST_ASSERT_TRUE_MESSAGE(g_sdReady, "SD Card not ready");

    Serial.println("\n[TEST] SD Card Flush Timing - Different Buffer Sizes");
    Serial.println("Bytes\tLines(est)\t\tFlush(μs)\tWith Open/Close(μs)");

    // Create CSV header
    const char *csvHeader = "time_ms,type,v1,v2,v3,v4,v5,v6\n";
    size_t headerLen = strlen(csvHeader);

    // Approximate line size based on CSV format
    // time_ms(5) + type(1) + v1-v6(6×5) + separators = ~45 bytes
    size_t lineSize = 45;

    // Test different buffer accumulations: 256B, 512B, 1KB, 2KB, 4KB
    unsigned int bufferSizes[] = {256, 512, 1024, 2048, 4096};

    for (int i = 0; i < 5; i++)
    {
        unsigned int bufferSize = bufferSizes[i];
        unsigned int estimatedLines = (bufferSize - headerLen) / lineSize;

        // Delete previous test file
        SD.remove("/sdcard_flush_test.csv");

        // Create file with header
        File f = SD.open("/sdcard_flush_test.csv", FILE_WRITE);
        f.write((const uint8_t *)csvHeader, headerLen);
        f.close();

        // Create dummy data to fill buffer
        char *dummyData = (char *)malloc(bufferSize);
        if (!dummyData)
        {
            Serial.println("[ERROR] Memory allocation failed");
            return;
        }

        // Generate CSV-like dummy data
        unsigned int dataSize = 0;
        for (unsigned int j = 0; j < estimatedLines && dataSize < bufferSize; j++)
        {
            int written = snprintf(dummyData + dataSize, bufferSize - dataSize,
                                   "1000,%c,%d,%d,%d,%d,%d,%d\n",
                                   (j % 2 == 0) ? 'I' : 'H', // IMU or HX711
                                   1000 + j, 2000 + j, 3000 + j,
                                   4000 + j, 5000 + j, 6000 + j);
            if (written <= 0)
                break;
            dataSize += written;
        }

        // Test 1: Flush only (file kept open)
        f = SD.open("/sdcard_flush_test.csv", FILE_APPEND);
        f.write((const uint8_t *)dummyData, dataSize);

        unsigned long flushStart = micros();
        f.flush();
        unsigned long flushTime = micros() - flushStart;

        f.close();

        // Test 2: Open + write + close (simulating direct flush strategy)
        SD.remove("/sdcard_flush_test.csv");
        f = SD.open("/sdcard_flush_test.csv", FILE_WRITE);
        f.write((const uint8_t *)csvHeader, headerLen);
        f.close();

        unsigned long openCloseStart = micros();
        f = SD.open("/sdcard_flush_test.csv", FILE_APPEND);
        f.write((const uint8_t *)dummyData, dataSize);
        f.close();
        unsigned long openCloseTime = micros() - openCloseStart;

        Serial.printf("%u\t%u\t\t%lu\t%lu\n",
                      bufferSize, estimatedLines, flushTime, openCloseTime);

        free(dummyData);
        delay(50);
    }

    Serial.println("[PASS] Flush timing test completed");
}

void setup()
{
    UNITY_BEGIN();

    RUN_TEST(test_sdcard_write_speed_different_block_sizes);
    RUN_TEST(test_sdcard_open_close_overhead);
    RUN_TEST(test_sdcard_flush_timing_different_sizes);

    UNITY_END();
}

void loop()
{
}
