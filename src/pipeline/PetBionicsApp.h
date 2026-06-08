#pragma once

#include <Arduino.h>
#include "../core/AppConfig.h"
#include "../core/AppTypes.h"
#include "../core/LocalClock.h"
#include "../sensors/RawSensor.h"
#include "../storage/RawSdLogger.h"
#include "../wifi/WifiManager.h"
#include "../web/WebInterface.h"
#include "LightFilter.h"
#include "OrientationEstimator.h"
#include "SimpleEventDetector.h"

class PetBionicsApp
{
public:
    PetBionicsApp();
    void begin();
    void update();

private:
    AppConfig _config;
    LocalClock _clock;
    RawSensor _sensor;
    LightFilter _filter;
    SimpleEventDetector _detector;
    OrientationEstimator _orientation;
    RawSdLogger _logger;
    WifiManager _wifi;
    WebInterface _web;

    uint32_t _sampleCursorUs;
    bool _loggingWasActive;
    AppStatus _status;
    bool _lowPowerModeActive;
    float _latestEstimatedKg;
    char _cmdBuffer[160];
    uint8_t _cmdLen;

    void processSample(uint32_t nowMs, uint32_t nowUs);
    void startSession();
    void stopSession();
    void handleSerialCommand();
    void enterLowPowerMode();
    void printSerialStatus() const;
    void printSerialFiles();
    void loadWifiCredentials();
    void saveWifiCredentials();
};
