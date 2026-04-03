#include "PetBionicsApp.h"

PetBionicsApp::PetBionicsApp()
    : _sensor(_config.analogPin),
      _filter(_config.filterAlpha),
      _detector(_config.eventThreshold, _config.eventCooldownMs),
      _logger(_config.sdCsPin, _config.sdPath),
      _ble(_config),
        _lastSampleUs(0),
        _wasAcquiring(false),
      _status{false, false, false, false, 0, 0} {}

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
