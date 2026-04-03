#include "PetBionicsApp.h"

PetBionicsApp::PetBionicsApp()
    : _sensor(_config.analogPin),
      _filter(_config.filterAlpha),
      _detector(_config.eventThreshold, _config.eventCooldownMs),
      _logger(_config.sdCsPin, _config.sdPath, _config.sdPendingRoot),
      _ble(_config),
      _wifi(),
      _cloud(),
      _pendingWifiSsid(""),
      _pendingWifiPass(""),
      _lastSampleUs(0),
      _wasAcquiring(false),
      _status{false, false, false, false, false, false, 0, 0} {}

void PetBionicsApp::begin()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("petBionics firmware started");

  _sensor.begin();
  _status.imuReady = _sensor.isImuReady();
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

  _wifi.begin();
  _cloud.begin();

  _ble.setCustomCommandHandler(&PetBionicsApp::customBleCommandBridge, this);
  _ble.begin("PetBionic");
}

void PetBionicsApp::update()
{
  uint32_t nowMs = _clock.nowMs();
  _sensor.updateHealth(nowMs);
  _logger.updateHealth(nowMs);
  _wifi.loop(nowMs);

  if (_config.acquisitionEnabled)
  {
    const uint32_t nowUs = micros();

    if (!_wasAcquiring)
    {
      _lastSampleUs = nowUs;
      _status.samples = 0;
      _status.events = 0;
      const uint64_t startEpochMs = _ble.currentEpochMs(nowMs);
      if (!_logger.startSession(startEpochMs))
      {
        Serial.println("Failed to start SD log session");
      }
      _wasAcquiring = true;
    }

    // Keep a fixed sampling timeline in microseconds to reduce jitter and avoid drift.
    // Re-check acquisitionEnabled every iteration so STOP can interrupt backlog catch-up quickly.
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
      _logger.stopSession();
    }
    _wasAcquiring = false;
  }

  _status.acquisitionEnabled = _config.acquisitionEnabled;
  _status.sdReady = _logger.isReady();
  _status.imuReady = _sensor.isImuReady();
  _status.hx711Ready = _sensor.isHx711Ready();
  _status.wifiConnected = _wifi.isConnected();
  _status.cloudConfigured = _cloud.isConfigured();
  _ble.updateStatus(_status, nowMs);
}

void PetBionicsApp::sampleStep(uint32_t nowMs, uint32_t nowUs)
{
  _filter.setAlpha(_config.filterAlpha);
  _detector.setThreshold(_config.eventThreshold);

  uint64_t epochMs = _ble.currentEpochMs(nowMs);
  int16_t ax = 0;
  int16_t ay = 0;
  int16_t az = 0;
  int16_t gx = 0;
  int16_t gy = 0;
  int16_t gz = 0;
  int16_t mx = 0;
  int16_t my = 0;
  int16_t mz = 0;

  int32_t raw = _sensor.readRaw();
  _sensor.readImuAxes(ax, ay, az, gx, gy, gz, mx, my, mz);
  float filtered = _filter.update(static_cast<float>(raw));
  EventInfo event = _detector.update(static_cast<float>(raw), filtered, nowMs);

  RawSample sample{nowMs, nowUs, epochMs, raw, filtered, ax, ay, az, gx, gy, gz, mx, my, mz};
  _logger.append(sample, event);

  _status.samples++;
  if (event.triggered)
  {
    _status.events++;
  }
}

bool PetBionicsApp::customBleCommandBridge(void *ctx, const String &cmd, String &ack)
{
  if (!ctx)
  {
    return false;
  }

  return static_cast<PetBionicsApp *>(ctx)->handleCustomBleCommand(cmd, ack);
}

bool PetBionicsApp::handleCustomBleCommand(const String &cmd, String &ack)
{
  Serial.printf("[App] BLE cmd='%s'\n", cmd.c_str());

  if (cmd.startsWith("WIFI_SSID="))
  {
    _pendingWifiSsid = cmd.substring(10);
    _pendingWifiSsid.trim();
    Serial.printf("[App] staged WiFi SSID='%s'\n", _pendingWifiSsid.c_str());
    ack = "WIFI_SSID";
    return true;
  }

  if (cmd.startsWith("WIFI_PASS="))
  {
    _pendingWifiPass = cmd.substring(10);
    _pendingWifiPass.trim();
    Serial.printf("[App] staged WiFi pass len=%u\n", static_cast<unsigned>(_pendingWifiPass.length()));
    ack = "WIFI_PASS";
    return true;
  }

  if (cmd.equalsIgnoreCase("WIFI_SAVE"))
  {
    if (_pendingWifiSsid.length() == 0)
    {
      Serial.println("[App] WiFi save rejected: missing SSID");
      ack = "WIFI_NO_SSID";
      return true;
    }

    Serial.println("[App] saving WiFi credentials");
    if (!_wifi.setCredentials(_pendingWifiSsid, _pendingWifiPass, true))
    {
      Serial.println("[App] WiFi save failed");
      ack = "WIFI_SAVE_FAIL";
      return true;
    }

    const bool connected = _wifi.connect(15000);
    Serial.printf("[App] WiFi save result connected=%s\n", connected ? "true" : "false");
    ack = connected ? "WIFI_OK" : "WIFI_CONN_FAIL";
    return true;
  }

  if (cmd.equalsIgnoreCase("WIFI_CONNECT"))
  {
    Serial.println("[App] WiFi connect requested");
    const bool connected = _wifi.connect(10000);
    Serial.printf("[App] WiFi connect result connected=%s\n", connected ? "true" : "false");
    ack = connected ? "WIFI_OK" : "WIFI_CONN_FAIL";
    return true;
  }

  if (cmd.equalsIgnoreCase("SYNC_LAST") ||
      cmd.startsWith("SYNC_FILE=") ||
      cmd.equalsIgnoreCase("SYNC_FILES") ||
      cmd.equalsIgnoreCase("SYNC_ALL"))
  {
    Serial.println("[App] sync requested for pending files");
    uint32_t syncedCount = 0;
    const bool ok = _cloud.syncPendingCsvFiles(String(_config.sdPendingRoot),
                                               String(_config.sdSentRoot),
                                               String(_config.deviceId),
                                               syncedCount);
    if (!ok)
    {
      Serial.printf("[App] sync failed count=%lu\n", static_cast<unsigned long>(syncedCount));
      ack = "SYNC_FAIL";
      return true;
    }

    Serial.printf("[App] sync completed count=%lu\n", static_cast<unsigned long>(syncedCount));
    ack = (syncedCount > 0) ? "SYNC_OK" : "SYNC_NONE";
    return true;
  }

  return false;
}
