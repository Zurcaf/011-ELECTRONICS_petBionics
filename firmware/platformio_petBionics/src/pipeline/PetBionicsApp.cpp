#include "PetBionicsApp.h"

#include <cstring>

namespace
{
    const char *basenameFromPath(const char *path)
    {
        if (!path || !*path)
        {
            return "run";
        }
        const char *lastSlash = strrchr(path, '/');
        return lastSlash ? lastSlash + 1 : path;
    }
} // namespace

PetBionicsApp::PetBionicsApp()
    : _sensor(_config.analogPin),
      _filter(_config.filterAlpha),
      _detector(_config.eventThreshold, _config.eventCooldownMs),
      _logger(_config.sdCsPin, _config.sdPath),
      _ble(_config),
      _lastSampleUs(0),
      _wasAcquiring(false),
      _status{false, false, false, false, 0, 0},
      _runStartLocalMs(0),
      _runStartEpochMs(0),
      _runImuFailureSeen(false),
      _runHx711FailureSeen(false)
{
    _runName[0]                  = '\0';
    _activeFilePathSnapshot[0]   = '\0';  // NEW
}

void PetBionicsApp::begin()
{
    Serial.begin(115200);
    delay(200);
    Serial.println("petBionics firmware started");

    _sensor.begin();
    _status.imuReady   = _sensor.isImuReady();
    _status.hx711Ready = _sensor.isHx711Ready();

    _status.sdReady = _logger.begin();
    if (_status.sdReady)
    {
        Serial.println("SD logger ready");
    }
    else
    {
        Serial.println("SD logger not ready (running without logging)");
    }

    _ble.begin("PetBionic");
}

void PetBionicsApp::update()
{
    uint32_t nowMs = _clock.nowMs();
    _sensor.updateHealth(nowMs);
    _logger.updateHealth(nowMs);

    if (_config.acquisitionEnabled)
    {
        const uint32_t nowUs = micros();

        if (!_wasAcquiring)
        {
            _lastSampleUs              = nowUs;
            _status.samples            = 0;
            _status.events             = 0;
            _runStartLocalMs           = nowMs;
            _runStartEpochMs           = _ble.currentEpochMs(nowMs);
            _runImuFailureSeen         = !_sensor.isImuReady();
            _runHx711FailureSeen       = !_sensor.isHx711Ready();
            _activeFilePathSnapshot[0] = '\0';  // NEW — reset snapshot
            _orientation.reset();

            if (!_logger.startSession(_runStartEpochMs))
            {
                Serial.println("Failed to start SD log session");
            }

            const char *activePath = _logger.activeFilePath();

            // NEW — snapshot the path before the session closes so
            // finalizeRun() can pass it to FirestoreSync safely.
            strncpy(_activeFilePathSnapshot, activePath ? activePath : "",
                    sizeof(_activeFilePathSnapshot) - 1);
            _activeFilePathSnapshot[sizeof(_activeFilePathSnapshot) - 1] = '\0';

            strncpy(_runName, basenameFromPath(activePath), sizeof(_runName) - 1);
            _runName[sizeof(_runName) - 1] = '\0';
            _wasAcquiring = true;
        }

        while (_config.acquisitionEnabled && (nowUs - _lastSampleUs) >= _config.samplePeriodUs)
        {
            _lastSampleUs += _config.samplePeriodUs;
            sampleStep(_lastSampleUs / 1000U, _lastSampleUs);
        }

        if (!_config.acquisitionEnabled)
        {
            _logger.stopSession();
            _wasAcquiring = false;
        }
    }
    else
    {
        if (_wasAcquiring)
        {
            finalizeRun(nowMs);
        }
        _wasAcquiring = false;
    }

    _status.acquisitionEnabled = _config.acquisitionEnabled;
    _status.sdReady            = _logger.isReady();
    _status.imuReady           = _sensor.isImuReady();
    _status.hx711Ready         = _sensor.isHx711Ready();
    _ble.updateStatus(_status, nowMs);
}

void PetBionicsApp::sampleStep(uint32_t nowMs, uint32_t nowUs)
{
    _filter.setAlpha(_config.filterAlpha);
    _detector.setThreshold(_config.eventThreshold);

    uint64_t epochMs = _ble.currentEpochMs(nowMs);
    int16_t ax = 0, ay = 0, az = 0;
    int16_t gx = 0, gy = 0, gz = 0;
    int16_t mx = 0, my = 0, mz = 0;

    int32_t raw      = _sensor.readRaw();
    _sensor.readImuAxes(ax, ay, az, gx, gy, gz, mx, my, mz);
    float filtered   = _filter.update(static_cast<float>(raw));
    EventInfo event  = _detector.update(static_cast<float>(raw), filtered, nowMs);

    const float dtSeconds  = static_cast<float>(_config.samplePeriodUs) / 1000000.0f;
    const Orientation orient = _orientation.update(ax, ay, az, gx, gy, gz, mx, my, mz, dtSeconds);

    RawSample sample{nowMs, nowUs, epochMs, raw, filtered,
                     ax, ay, az, gx, gy, gz, mx, my, mz,
                     orient.roll, orient.pitch, orient.yaw};
    _logger.append(sample, event);

    _status.samples++;
    if (!_sensor.isImuReady())   { _runImuFailureSeen   = true; }
    if (!_sensor.isHx711Ready()) { _runHx711FailureSeen = true; }
    if (event.triggered)         { _status.events++; }
}

const char *PetBionicsApp::sessionRunName() const
{
    return _runName[0] != '\0' ? _runName : "run";
}

// ---------------------------------------------------------------------------
// finalizeRun — called once when acquisition transitions to OFF.
// Closes the SD session, optionally syncs to Firestore, then publishes the
// run summary over BLE (includes sync result when WiFi is configured).
// ---------------------------------------------------------------------------
void PetBionicsApp::finalizeRun(uint32_t nowMs)
{
    // Build sensor-failure string
    char failures[64];
    failures[0] = '\0';
    bool firstFailure = true;

    if (_runImuFailureSeen)
    {
        strncat(failures, "IMU", sizeof(failures) - strlen(failures) - 1);
        firstFailure = false;
    }
    if (_runHx711FailureSeen)
    {
        if (!firstFailure)
        {
            strncat(failures, ", ", sizeof(failures) - strlen(failures) - 1);
        }
        strncat(failures, "Load Cell", sizeof(failures) - strlen(failures) - 1);
    }
    if (failures[0] == '\0')
    {
        strncpy(failures, "none", sizeof(failures) - 1);
        failures[sizeof(failures) - 1] = '\0';
    }

    const uint32_t durationMs =
        (nowMs >= _runStartLocalMs) ? (nowMs - _runStartLocalMs) : 0;

    // Session name used as Firestore sessionId.
    const String sessionId = String(sessionRunName());

    // The file path was snapshotted at session-start so we can safely use it
    // after stopSession() clears the logger's internal copy.
    const String filePath = String(_activeFilePathSnapshot);

    // SD session must be closed before we open the file for reading.
    _logger.stopSession();

    // ── WiFi + Firestore sync ─────────────────────────────────────────────
    bool     syncAttempted = false;
    bool     syncOk        = false;
    int      syncReadings  = 0;

    if (_config.wifiEnabled &&
        _config.wifiSsid[0] != '\0' &&
        filePath.length() > 0)
    {
        syncAttempted = true;
        Serial.println("[App] Starting WiFi sync...");

        if (_wifi.connect(_config.wifiSsid, _config.wifiPassword))
        {
            SyncResult result = _sync.syncFile(filePath.c_str(), sessionId);
            syncOk       = result.success;
            syncReadings = result.readingsSynced;

            if (syncOk)
            {
                // Move file from /inbox to /sent on the SD card.
                _logger.markAsSent(filePath.c_str());
            }
            else
            {
                Serial.printf("[App] Sync failed (code %d) — data safe on SD\n",
                              result.httpErrorCode);
            }

            _wifi.disconnect();
        }
        else
        {
            Serial.println("[App] WiFi connect failed — data safe on SD");
        }
    }
    else if (!_config.wifiEnabled)
    {
        Serial.println("[App] WiFi not configured — skipping sync");
    }

    // ── BLE run-summary notification ─────────────────────────────────────
    char summary[512];
    snprintf(summary,
             sizeof(summary),
             "{"
             "\"run_complete\":true,"
             "\"run_name\":\"%s\","
             "\"samples_final\":%lu,"
             "\"duration_ms\":%lu,"
             "\"imu_failure\":%s,"
             "\"hx711_failure\":%s,"
             "\"sensor_failures\":\"%s\","
             "\"sync_attempted\":%s,"
             "\"sync_ok\":%s,"
             "\"sync_readings\":%d"
             "}",
             sessionRunName(),
             static_cast<unsigned long>(_status.samples),
             static_cast<unsigned long>(durationMs),
             _runImuFailureSeen   ? "true" : "false",
             _runHx711FailureSeen ? "true" : "false",
             failures,
             syncAttempted ? "true" : "false",
             syncOk        ? "true" : "false",
             syncReadings);

    _ble.publishRunSummary(String(summary), nowMs);

    // Reset run state
    _runName[0]                = '\0';
    _activeFilePathSnapshot[0] = '\0';
    _runStartLocalMs           = 0;
    _runStartEpochMs           = 0;
    _runImuFailureSeen         = false;
    _runHx711FailureSeen       = false;
}