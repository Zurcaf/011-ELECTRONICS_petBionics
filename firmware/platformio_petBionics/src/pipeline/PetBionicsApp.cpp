#include "PetBionicsApp.h"

#include <time.h>
#include <cstring>
#include <esp_sleep.h>

PetBionicsApp::PetBionicsApp()
    : _sensor(_config.analogPin),
      _filter(_config.filterAlpha),
      _detector(_config.eventThreshold, _config.eventCooldownMs),
      _logger(_config.sdCsPin, _config.sdPath),
      _web(_config, _status),
      _lastSampleUs(0),
      _wasAcquiring(false),
      _status{false, false, false, false, 0, 0, 0.0f},
      _sleepMode(false)
{
}

void PetBionicsApp::begin()
{
    // Ensure GPIO0 is pulled high (boot mode, not download mode)
    pinMode(0, INPUT_PULLUP);
    delay(50);

    Serial.begin(115200);
    Serial.setTimeout(100);  // Non-blocking Serial (battery mode)
    delay(100);

    // Flush any garbage from Serial
    while (Serial.available())
    {
        Serial.read();
    }

    Serial.println("=== petBionics firmware started ===");

    Serial.println("[Init] Sensors...");
    _sensor.begin();
    _status.imuReady   = _sensor.isImuReady();
    _status.hx711Ready = _sensor.isHx711Ready();
    Serial.printf("[Init] IMU:   %s\n", _status.imuReady   ? "OK" : "FAIL");
    Serial.printf("[Init] HX711: %s\n", _status.hx711Ready ? "OK" : "FAIL");

    Serial.println("[Init] SD...");
    _status.sdReady = _logger.begin();
    Serial.printf("[Init] SD:    %s\n", _status.sdReady ? "OK" : "FAIL");

    Serial.println("[Init] WiFi...");
    if (_wifi.connect(_config.wifiSsid, _config.wifiPassword))
    {
        _web.begin();
        Serial.printf("[Init] Web UI: http://%s\n", _wifi.localIP().toString().c_str());

        // Sync time via NTP (UTC; RawSdLogger's TZ env var converts to local)
        Serial.println("[Init] Syncing time via NTP...");
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");

        const uint32_t ntpTimeoutMs = 5000;
        const uint32_t ntpStart = millis();
        while ((millis() - ntpStart) < ntpTimeoutMs)
        {
            time_t now = time(nullptr);
            // If time is before 2000-01-01, NTP hasn't synced yet
            if (now > 946684800)
            {
                Serial.printf("[Init] Time synced: %s", ctime(&now));
                break;
            }
            delay(100);
        }
    }
    else
    {
        Serial.println("[Init] WiFi failed — web UI unavailable, logging still works");
    }

    Serial.println("[Init] Ready — aguarda comando na web UI");
}

void PetBionicsApp::update()
{
    // Check for serial commands (sleep/wakeup)
    processSerialCommand();

    // If in sleep mode, skip main loop
    if (_sleepMode)
        return;

    const uint32_t nowMs = _clock.nowMs();
    _sensor.updateHealth(nowMs);
    _logger.updateHealth(nowMs);
    _web.update();

    // Read battery voltage every 2 seconds
    static uint32_t lastBatteryReadMs = 0;
    if ((nowMs - lastBatteryReadMs) >= 2000)
    {
        lastBatteryReadMs = nowMs;
        int adcRaw = analogRead(_config.batteryAdcPin);
        float adcVoltage = (float)adcRaw * 3.3f / 4095.0f;
        _status.batteryVoltage = adcVoltage * _config.batteryCalibration;

        Serial.printf("[Batt] ADC_raw=%d, ADC_V=%.3f, Batt_V=%.2f (factor=%.1f)\n",
                      adcRaw, adcVoltage, _status.batteryVoltage, _config.batteryCalibration);
    }

    if (_config.acquisitionEnabled)
    {
        if (!_wasAcquiring)
        {
            startSession();
        }

        const uint32_t nowUs = micros();
        while ((nowUs - _lastSampleUs) >= _config.samplePeriodUs)
        {
            _lastSampleUs += _config.samplePeriodUs;
            sampleStep(_lastSampleUs / 1000U, _lastSampleUs);
        }
    }
    else
    {
        if (_wasAcquiring)
        {
            stopSession();
        }
    }

    _status.acquisitionEnabled = _config.acquisitionEnabled;
    _status.sdReady            = _logger.isReady();
    _status.imuReady           = _sensor.isImuReady();
    _status.hx711Ready         = _sensor.isHx711Ready();
}

void PetBionicsApp::startSession()
{
    Serial.println("[Run] START — nova sessão");
    _status.samples  = 0;
    _status.events   = 0;
    _lastSampleUs    = micros();
    _orientation.reset();

    // Get current time for session filename
    time_t now = time(nullptr);
    uint64_t epochMs = (uint64_t)now * 1000ULL;

    if (!_logger.startSession(epochMs))
    {
        Serial.println("[Run] Falha ao criar sessão no SD");
    }
    else
    {
        Serial.printf("[Run] Ficheiro: %s\n", _logger.activeFilePath());
    }

    _wasAcquiring = true;
}

void PetBionicsApp::stopSession()
{
    _logger.stopSession();
    _wasAcquiring = false;
    Serial.printf("[Run] STOP — %lu amostras gravadas\n",
                  static_cast<unsigned long>(_status.samples));
}

void PetBionicsApp::processSerialCommand()
{
    static char cmdBuffer[32] = {0};
    static uint8_t cmdLen = 0;

    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n' || c == '\r')
        {
            if (cmdLen > 0)
            {
                cmdBuffer[cmdLen] = '\0';
                Serial.printf("[CMD] '%s'\n", cmdBuffer);

                if (strcmp(cmdBuffer, "sleep") == 0)
                {
                    if (!_sleepMode)
                    {
                        enterLightSleep();
                    }
                }
                else if (strcmp(cmdBuffer, "wakeup") == 0)
                {
                    if (_sleepMode)
                    {
                        _sleepMode = false;
                    }
                }

                cmdLen = 0;
            }
        }
        else if (cmdLen < sizeof(cmdBuffer) - 1)
        {
            cmdBuffer[cmdLen++] = c;
        }
    }
}

void PetBionicsApp::enterLightSleep()
{
    _sleepMode = true;

    // Stop logging if active
    if (_wasAcquiring)
    {
        _logger.stopSession();
        _wasAcquiring = false;
    }

    // Disconnect WiFi to save power
    WiFi.disconnect(true);  // true = turn off WiFi radio
    Serial.println("[Sleep] WiFi disabled, low-power mode active");
    Serial.flush();

    // Loop in sleep mode until wakeup received
    while (_sleepMode)
    {
        delay(100);  // Check serial every 100ms
        processSerialCommand();  // This will set _sleepMode = false on "wakeup"
    }

    // On wakeup: reconnect WiFi
    Serial.println("[Sleep] Resuming normal operation...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(_config.wifiSsid, _config.wifiPassword);

    uint32_t wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart) < 10000)
    {
        delay(200);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("[WiFi] Reconnected — http://%s\n", WiFi.localIP().toString().c_str());
    }
    else
    {
        Serial.println("[WiFi] Reconnection failed, continuing without WiFi");
    }
}

void PetBionicsApp::sampleStep(uint32_t nowMs, uint32_t nowUs)
{
    _filter.setAlpha(_config.filterAlpha);
    _detector.setThreshold(_config.eventThreshold);

    int16_t ax = 0, ay = 0, az = 0;
    int16_t gx = 0, gy = 0, gz = 0;
    int16_t mx = 0, my = 0, mz = 0;

    int32_t   raw      = _sensor.readRaw();
    _sensor.readImuAxes(ax, ay, az, gx, gy, gz, mx, my, mz);
    float     filtered = _filter.update(static_cast<float>(raw));
    EventInfo event    = _detector.update(static_cast<float>(raw), filtered, nowMs);

    const float       dtSeconds = static_cast<float>(_config.samplePeriodUs) / 1000000.0f;
    const Orientation orient    = _orientation.update(ax, ay, az, gx, gy, gz, mx, my, mz, dtSeconds);

    RawSample sample{nowMs, nowUs, 0ULL, raw, filtered,
                     ax, ay, az, gx, gy, gz, mx, my, mz,
                     orient.roll, orient.pitch, orient.yaw};
    _logger.append(sample, event);

    _status.samples++;
    if (event.triggered) { _status.events++; }
}
