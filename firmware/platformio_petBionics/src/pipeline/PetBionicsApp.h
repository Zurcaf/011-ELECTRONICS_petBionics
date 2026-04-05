#pragma once

#include <Arduino.h>
#include "../ble/BleControl.h"
#include "../core/AppConfig.h"
#include "../core/AppTypes.h"
#include "../core/LocalClock.h"
#include "../sensors/RawSensor.h"
#include "../storage/RawSdLogger.h"
#include "../wifi/WifiManager.h"       // NEW
#include "../sync/FirestoreSync.h"     // NEW
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
    AppConfig            _config;
    LocalClock           _clock;
    RawSensor            _sensor;
    LightFilter          _filter;
    SimpleEventDetector  _detector;
    OrientationEstimator _orientation;
    RawSdLogger          _logger;
    BleControl           _ble;
    WifiManager          _wifi;        // NEW
    FirestoreSync        _sync;        // NEW

    uint32_t _lastSampleUs;
    bool     _wasAcquiring;
    AppStatus _status;
    uint32_t _runStartLocalMs;
    uint64_t _runStartEpochMs;
    bool     _runImuFailureSeen;
    bool     _runHx711FailureSeen;
    char     _runName[96];

    // Path of the active session file — captured at startSession() so
    // finalizeRun() can pass it to FirestoreSync after stopSession().
    char _activeFilePathSnapshot[96];  // NEW

    void sampleStep(uint32_t nowMs, uint32_t nowUs);
    void finalizeRun(uint32_t nowMs);
    const char *sessionRunName() const;
};