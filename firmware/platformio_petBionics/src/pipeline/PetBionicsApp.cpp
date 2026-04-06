#include "PetBionicsApp.h"

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <BLEDevice.h>
#include "../wifi/WifiManager.h"
#include "../sync/FirestoreSync.h"

namespace
{
// ---------------------------------------------------------------------------
// Shared credential block used by both sync tasks.
// ---------------------------------------------------------------------------
struct SyncCredentials
{
    char ssid[64];
    char password[64];
};

// ---------------------------------------------------------------------------
// Single-file sync task — runs after a session closes automatically.
// ---------------------------------------------------------------------------
struct SyncTaskParams
{
    char         filePath[96];
    char         sessionId[96];
    SyncCredentials creds;
    RawSdLogger *logger;
    AppConfig   *config;
};

void firestoreSyncTask(void *arg)
{
    auto *p = static_cast<SyncTaskParams *>(arg);
    WifiManager   wifi;
    FirestoreSync sync;

    if (wifi.connect(p->creds.ssid, p->creds.password))
    {
        SyncResult r = sync.syncFile(p->filePath, String(p->sessionId));
        if (r.success)
        {
            p->logger->markAsSent(p->filePath);
            p->config->syncResultCode = 1;
        }
        else
        {
            Serial.printf("[Sync] Failed (HTTP %d) — data safe on SD\n", r.httpErrorCode);
            p->config->syncResultCode = 2;
        }
        wifi.disconnect();
    }
    else
    {
        Serial.println("[Sync] WiFi connect failed — data safe on SD");
        p->config->syncResultCode = 2;
    }

    delete p;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Sync-all task — walks /inbox/** and uploads every pending CSV file.
// ---------------------------------------------------------------------------
struct SyncAllTaskParams
{
    SyncCredentials creds;
    RawSdLogger    *logger;
    AppConfig      *config;
};

// Returns true if name ends in .csv (case-insensitive for FAT short names).
static bool isCsvFile(const char *name)
{
    if (!name) return false;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return strcmp(dot, ".csv") == 0 || strcmp(dot, ".CSV") == 0;
}

// Walk /inbox/<day>/<file>.csv and sync each one.
// Returns the number of files successfully uploaded.
static int syncAllInbox(FirestoreSync &sync, RawSdLogger *logger)
{
    int synced = 0;

    File inbox = SD.open("/inbox");
    if (!inbox) { Serial.println("[SyncAll] /inbox not found"); return 0; }

    File dayDir = inbox.openNextFile();
    while (dayDir)
    {
        if (dayDir.isDirectory())
        {
            char dayPath[64];
            snprintf(dayPath, sizeof(dayPath), "/inbox/%s", dayDir.name());

            File csv = dayDir.openNextFile();
            while (csv)
            {
                char filePath[128];
                char sessionId[64];
                bool doSync = false;

                if (!csv.isDirectory() && isCsvFile(csv.name()))
                {
                    snprintf(filePath, sizeof(filePath), "%s/%s", dayPath, csv.name());
                    strncpy(sessionId, csv.name(), sizeof(sessionId) - 1);
                    sessionId[sizeof(sessionId) - 1] = '\0';
                    char *dotPos = strrchr(sessionId, '.');
                    if (dotPos) *dotPos = '\0';
                    doSync = true;
                }
                csv.close();

                if (doSync)
                {
                    Serial.printf("[SyncAll] uploading %s\n", filePath);
                    SyncResult r = sync.syncFile(filePath, String(sessionId));
                    if (r.success)
                    {
                        logger->markAsSent(filePath);
                        synced++;
                        Serial.printf("[SyncAll] ok → moved to /sent (%d so far)\n", synced);
                    }
                    else
                    {
                        Serial.printf("[SyncAll] failed HTTP %d, keeping in /inbox\n", r.httpErrorCode);
                    }
                }

                csv = dayDir.openNextFile();
            }
        }
        dayDir.close();
        dayDir = inbox.openNextFile();
    }
    inbox.close();
    return synced;
}

void firestoreSyncAllTask(void *arg)
{
    auto *p = static_cast<SyncAllTaskParams *>(arg);
    WifiManager   wifi;
    FirestoreSync sync;

    // Free BLE heap so TLS handshake (~40 KB) has room to allocate.
    Serial.println("[SyncAll] Deinit BLE to free heap for TLS...");
    BLEDevice::deinit(true);
    delay(100);

    if (!wifi.connect(p->creds.ssid, p->creds.password))
    {
        Serial.println("[SyncAll] WiFi connect failed");
        p->config->syncResultCode  = 2;
        p->config->bleRestartNeeded = true;
        delete p;
        vTaskDelete(nullptr);
        return;
    }

    int sent = syncAllInbox(sync, p->logger);
    wifi.disconnect();

    p->config->syncSentCount    = sent;
    p->config->syncResultCode   = (sent > 0) ? 1 : 5;
    p->config->bleRestartNeeded = true;  // signal main loop to restart BLE
    Serial.printf("[SyncAll] Done — %d files uploaded\n", sent);

    delete p;
    vTaskDelete(nullptr);
}

// Derives the Firestore session ID from a file path (basename without .csv).
static String sessionIdFromPath(const char *path)
{
    if (!path || !*path) return String("session");
    const char *lastSlash = strrchr(path, '/');
    const char *base      = lastSlash ? lastSlash + 1 : path;
    String s(base);
    if (s.endsWith(".csv")) s = s.substring(0, s.length() - 4);
    return s;
}

// Spawns a single-file sync task (used for auto-sync after session end).
static bool spawnSyncTask(const char *filePath, AppConfig &config, RawSdLogger &logger)
{
    if (!config.wifiEnabled || config.wifiSsid[0] == '\0')
    {
        config.syncResultCode = 3; // no wifi
        return false;
    }
    if (!filePath || filePath[0] == '\0')
    {
        config.syncResultCode = 5; // no pending
        return false;
    }

    auto *params = new SyncTaskParams{};
    strncpy(params->filePath,        filePath,           sizeof(params->filePath)  - 1);
    String sid = sessionIdFromPath(filePath);
    strncpy(params->sessionId,       sid.c_str(),         sizeof(params->sessionId) - 1);
    strncpy(params->creds.ssid,      config.wifiSsid,     sizeof(params->creds.ssid)     - 1);
    strncpy(params->creds.password,  config.wifiPassword, sizeof(params->creds.password) - 1);
    params->filePath[sizeof(params->filePath)   - 1] = '\0';
    params->sessionId[sizeof(params->sessionId) - 1] = '\0';
    params->creds.ssid[sizeof(params->creds.ssid)         - 1] = '\0';
    params->creds.password[sizeof(params->creds.password) - 1] = '\0';
    params->logger = &logger;
    params->config = &config;

    if (xTaskCreate(firestoreSyncTask, "fsSync", 8192, params, 1, nullptr) != pdPASS)
    {
        Serial.println("[App] Could not create sync task");
        delete params;
        config.syncResultCode = 2;
        return false;
    }
    return true;
}

// Spawns a sync-all task that uploads every file in /inbox.
static bool spawnSyncAllTask(AppConfig &config, RawSdLogger &logger)
{
    if (!config.wifiEnabled || config.wifiSsid[0] == '\0')
    {
        config.syncResultCode = 3; // no wifi
        return false;
    }

    auto *params = new SyncAllTaskParams{};
    strncpy(params->creds.ssid,      config.wifiSsid,     sizeof(params->creds.ssid)     - 1);
    strncpy(params->creds.password,  config.wifiPassword, sizeof(params->creds.password) - 1);
    params->creds.ssid[sizeof(params->creds.ssid)         - 1] = '\0';
    params->creds.password[sizeof(params->creds.password) - 1] = '\0';
    params->logger = &logger;
    params->config = &config;

    if (xTaskCreate(firestoreSyncAllTask, "fsSyncAll", 8192, params, 1, nullptr) != pdPASS)
    {
        Serial.println("[App] Could not create sync-all task");
        delete params;
        config.syncResultCode = 2;
        return false;
    }
    return true;
}

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
    Serial.println("=== petBionics firmware started ===");

    Serial.println("[Init] Sensors...");
    _sensor.begin();
    _status.imuReady   = _sensor.isImuReady();
    _status.hx711Ready = _sensor.isHx711Ready();
    Serial.printf("[Init] IMU:   %s\n", _status.imuReady   ? "OK" : "FAIL");
    Serial.printf("[Init] HX711: %s\n", _status.hx711Ready ? "OK" : "FAIL");

    Serial.println("[Init] SD...");
    _status.sdReady = _logger.begin();
    Serial.printf("[Init] SD:    %s\n", _status.sdReady ? "OK" : "FAIL (logging disabled)");

    Serial.println("[Init] BLE...");
    _ble.begin("PetBionic");
    Serial.println("[Init] BLE advertising started");

    Serial.println("[Init] Ready. Waiting for BLE connection...");
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
            Serial.println("[Run] START — session beginning");
            _lastSampleUs              = nowUs;
            _status.samples            = 0;
            _status.events             = 0;
            _runStartLocalMs           = nowMs;
            _runStartEpochMs           = _ble.currentEpochMs(nowMs);
            _runImuFailureSeen         = !_sensor.isImuReady();
            _runHx711FailureSeen       = !_sensor.isHx711Ready();
            _activeFilePathSnapshot[0] = '\0';
            _orientation.reset();

            if (!_logger.startSession(_runStartEpochMs))
            {
                Serial.println("[Run] Failed to start SD log session");
            }

            const char *activePath = _logger.activeFilePath();
            Serial.printf("[Run] Session file: %s\n", activePath ? activePath : "(none)");

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

    // ── Handle manual SYNC command (uploads ALL pending files in /inbox) ────
    if (_config.syncRequested && !_wasAcquiring)
    {
        _config.syncRequested = false;
        Serial.println("[App] SYNC command → scanning /inbox for pending files");
        if (spawnSyncAllTask(_config, _logger))
        {
            Serial.println("[App] Sync-all task started");
        }
    }

    // ── Restart BLE after sync task freed it ─────────────────────────────
    if (_config.bleRestartNeeded)
    {
        _config.bleRestartNeeded = false;
        Serial.println("[App] Restarting BLE after sync...");
        _ble.begin("PetBionic");
        Serial.println("[App] BLE restarted");
    }

    // ── Report sync result to BLE app ─────────────────────────────────────
    if (_config.syncResultCode != 0)
    {
        int code  = _config.syncResultCode;
        int count = _config.syncSentCount;
        _config.syncResultCode = 0;
        _config.syncSentCount  = 0;
        const char *result =
            (code == 1) ? "ok" :
            (code == 3) ? "no_wifi" :
            (code == 5) ? "no_pending" : "fail";
        char notif[96];
        snprintf(notif, sizeof(notif),
                 "{\"sync_result\":\"%s\",\"synced\":%d}", result, count);
        _ble.publishRunSummary(String(notif), nowMs);
        Serial.printf("[App] Sync result: %s (%d sent)\n", result, count);
    }
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
// Closes the SD session, publishes the BLE run summary immediately, then
// spawns a background FreeRTOS task for WiFi/Firestore sync so that the
// main loop (and BLE) remain responsive during the upload.
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

    // Snapshot state before resetting (sessionRunName() reads _runName).
    const String sessionId = String(sessionRunName());
    const String filePath  = String(_activeFilePathSnapshot);

    // Close SD session before BLE publish and before any file reads.
    _logger.stopSession();

    // ── BLE run-summary — published immediately so the app isn't kept waiting
    char summary[384];
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
             "\"sync_pending\":%s"
             "}",
             sessionRunName(),
             static_cast<unsigned long>(_status.samples),
             static_cast<unsigned long>(durationMs),
             _runImuFailureSeen   ? "true" : "false",
             _runHx711FailureSeen ? "true" : "false",
             failures,
             (_config.wifiEnabled && _config.wifiSsid[0] != '\0') ? "true" : "false");

    _ble.publishRunSummary(String(summary), nowMs);

    // Persist file path for possible manual re-sync via SYNC command.
    strncpy(_config.lastSessionFilePath, filePath.c_str(),
            sizeof(_config.lastSessionFilePath) - 1);
    _config.lastSessionFilePath[sizeof(_config.lastSessionFilePath) - 1] = '\0';

    // Reset run state before spawning task (task uses its own copies).
    _runName[0]                = '\0';
    _activeFilePathSnapshot[0] = '\0';
    _runStartLocalMs           = 0;
    _runStartEpochMs           = 0;
    _runImuFailureSeen         = false;
    _runHx711FailureSeen       = false;

    // ── Spawn background WiFi sync task ──────────────────────────────────
    if (filePath.length() > 0)
    {
        if (spawnSyncTask(filePath.c_str(), _config, _logger))
        {
            Serial.println("[App] Firestore sync started in background");
        }
    }
}
