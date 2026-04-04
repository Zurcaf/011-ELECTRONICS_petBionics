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
  _runName[0] = '\0';
}

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
      _lastSampleUs = nowUs;
      _status.samples = 0;
      _status.events = 0;
      _runStartLocalMs = nowMs;
      _runStartEpochMs = _ble.currentEpochMs(nowMs);
      _runImuFailureSeen = !_sensor.isImuReady();
      _runHx711FailureSeen = !_sensor.isHx711Ready();
      _orientation.reset();
      if (!_logger.startSession(_runStartEpochMs))
      {
        Serial.println("Failed to start SD log session");
      }
      const char *activePath = _logger.activeFilePath();
      strncpy(_runName, basenameFromPath(activePath), sizeof(_runName) - 1);
      _runName[sizeof(_runName) - 1] = '\0';
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
      finalizeRun(nowMs);
    }
    _wasAcquiring = false;
  }

  _status.acquisitionEnabled = _config.acquisitionEnabled;
  _status.sdReady = _logger.isReady();
  _status.imuReady = _sensor.isImuReady();
  _status.hx711Ready = _sensor.isHx711Ready();
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

  const float dtSeconds = static_cast<float>(_config.samplePeriodUs) / 1000000.0f;
  const Orientation orient = _orientation.update(ax, ay, az, gx, gy, gz, mx, my, mz, dtSeconds);

  RawSample sample{nowMs, nowUs, epochMs, raw, filtered,
                   ax, ay, az, gx, gy, gz, mx, my, mz,
                   orient.roll, orient.pitch, orient.yaw};
  _logger.append(sample, event);

  _status.samples++;
  if (!_sensor.isImuReady())
  {
    _runImuFailureSeen = true;
  }
  if (!_sensor.isHx711Ready())
  {
    _runHx711FailureSeen = true;
  }
  if (event.triggered)
  {
    _status.events++;
  }
}

const char *PetBionicsApp::sessionRunName() const
{
  return _runName[0] != '\0' ? _runName : "run";
}

void PetBionicsApp::finalizeRun(uint32_t nowMs)
{
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

  const uint32_t durationMs = (nowMs >= _runStartLocalMs) ? (nowMs - _runStartLocalMs) : 0;

  char summary[384];
  snprintf(summary,
           sizeof(summary),
           "{\"run_complete\":true,\"run_name\":\"%s\",\"samples_final\":%lu,\"duration_ms\":%lu,\"imu_failure\":%s,\"hx711_failure\":%s,\"sensor_failures\":\"%s\"}",
           sessionRunName(),
           static_cast<unsigned long>(_status.samples),
           static_cast<unsigned long>(durationMs),
           _runImuFailureSeen ? "true" : "false",
           _runHx711FailureSeen ? "true" : "false",
           failures);

  _logger.stopSession();
  _ble.publishRunSummary(String(summary), nowMs);

  _runName[0] = '\0';
  _runStartLocalMs = 0;
  _runStartEpochMs = 0;
  _runImuFailureSeen = false;
  _runHx711FailureSeen = false;
}
