#include <Arduino.h>
#include <SD.h>

#include "ble/BleControl.h"
#include "core/AppConfig.h"
#include "core/Pinout.h"
#include "core/AppTypes.h"
#include "pipeline/LightFilter.h"
#include "pipeline/PetBionicsApp.h"
#include "pipeline/SimpleEventDetector.h"
#include "sensors/RawSensor.h"
#include "storage/RawSdLogger.h"

namespace
{
    AppConfig config;
    RawSensor rawSensor(config.analogPin);
    LightFilter lightFilter(0.2f);
    SimpleEventDetector eventDetector(100.0f, 300);
    RawSdLogger sdLogger(config.sdCsPin, "/diag_raw_log.csv");
    BleControl bleControl(config);

    void printMenu()
    {
        Serial.println();
        Serial.println("=== petBionics diagnostic main ===");
        Serial.println("1 - RawSensor");
        Serial.println("2 - LightFilter");
        Serial.println("3 - SimpleEventDetector");
        Serial.println("4 - RawSdLogger");
        Serial.println("5 - BleControl");
        Serial.println("6 - Full app smoke test");
        Serial.println("m - show menu");
    }

    void runRawSensorTest()
    {
        Serial.println();
        Serial.println("[TEST] RawSensor");
        for (int i = 0; i < 10; ++i)
        {
            int32_t raw = rawSensor.readRaw();
            Serial.printf("sample[%d] = %ld\n", i, static_cast<long>(raw));
            delay(200);
        }
    }

    void runLightFilterTest()
    {
        Serial.println();
        Serial.println("[TEST] LightFilter");
        LightFilter filter(0.25f);
        const float inputs[] = {0.0f, 100.0f, 100.0f, 20.0f, 80.0f, 0.0f};

        for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
        {
            float output = filter.update(inputs[i]);
            Serial.printf("in=%.2f out=%.2f initialized=%s\n",
                          inputs[i],
                          output,
                          filter.initialized() ? "true" : "false");
        }
    }

    void runEventDetectorTest()
    {
        Serial.println();
        Serial.println("[TEST] SimpleEventDetector");
        SimpleEventDetector detector(15.0f, 300);
        const struct
        {
            float raw;
            float filtered;
            uint32_t deltaMs;
        } steps[] = {
            {100.0f, 95.0f, 0},
            {160.0f, 100.0f, 100},
            {162.0f, 110.0f, 100},
            {180.0f, 120.0f, 350},
            {150.0f, 149.0f, 350},
        };

        uint32_t nowMs = 0;
        for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
        {
            nowMs += steps[i].deltaMs;
            EventInfo event = detector.update(steps[i].raw, steps[i].filtered, nowMs);
            Serial.printf("raw=%.2f filtered=%.2f score=%.2f triggered=%s now=%lu\n",
                          steps[i].raw,
                          steps[i].filtered,
                          event.score,
                          event.triggered ? "true" : "false",
                          static_cast<unsigned long>(nowMs));
        }
    }

    void runSdLoggerTest()
    {
        Serial.println();
        Serial.println("[TEST] RawSdLogger");
        SD.remove("/diag_raw_log.csv");

        bool ready = sdLogger.begin();
        Serial.printf("logger.begin() -> %s\n", ready ? "OK" : "FAIL");
        if (!ready)
        {
            Serial.println("SD logger did not initialize. Check CS pin, wiring and card format.");
            return;
        }

        RawSample sample{millis(), 0U, 1234ULL, 98, 98.76f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f};
        EventInfo event{true, 45.67f};
        bool appended = sdLogger.append(sample, event);
        Serial.printf("logger.append() -> %s\n", appended ? "OK" : "FAIL");
    }

    void runBleControlTest()
    {
        Serial.println();
        Serial.println("[TEST] BleControl");
        Serial.printf("before: acq=%s alpha=%.3f thr=%.2f period=%lu\n",
                      config.acquisitionEnabled ? "true" : "false",
                      config.filterAlpha,
                      config.eventThreshold,
                      static_cast<unsigned long>(config.samplePeriodUs));

        bleControl.begin("petBionics-diag");
        bleControl.applyCommand("STOP");
        bleControl.applyCommand("ALPHA=0.35");
        bleControl.applyCommand("THR=42.5");
        bleControl.applyCommand("PERIOD=50");
        bleControl.applyCommand("START");

        Serial.printf("after:  acq=%s alpha=%.3f thr=%.2f period=%lu\n",
                      config.acquisitionEnabled ? "true" : "false",
                      config.filterAlpha,
                      config.eventThreshold,
                      static_cast<unsigned long>(config.samplePeriodUs));
    }

    void runAppSmokeTest()
    {
        Serial.println();
        Serial.println("[TEST] PetBionicsApp smoke test");
        PetBionicsApp app;
        app.begin();
        for (int i = 0; i < 5; ++i)
        {
            app.update();
            delay(100);
        }
        Serial.println("App smoke test finished.");
    }

    void runSelectedTest(char command)
    {
        switch (command)
        {
        case '1':
            runRawSensorTest();
            break;
        case '2':
            runLightFilterTest();
            break;
        case '3':
            runEventDetectorTest();
            break;
        case '4':
            runSdLoggerTest();
            break;
        case '5':
            runBleControlTest();
            break;
        case '6':
            runAppSmokeTest();
            break;
        case 'm':
        case 'M':
            printMenu();
            break;
        default:
            Serial.println("Unknown command. Press m for menu.");
            break;
        }
    }
} // namespace

void setup()
{
    Serial.begin(115200);
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 3000))
    {
        delay(10);
    }

    rawSensor.begin();
    printMenu();
    Serial.println("Change the active test by sending 1..6 over serial.");
}

void loop()
{
    if (!Serial.available())
    {
        delay(10);
        return;
    }

    char command = static_cast<char>(Serial.read());
    if (command == '\r' || command == '\n' || command == ' ')
    {
        return;
    }

    runSelectedTest(command);
}
