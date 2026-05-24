#include "PetBionicsApp.h"

#include <time.h>
#include <cstring>
#include <math.h>
#include <esp_sleep.h>
#include <Preferences.h>

PetBionicsApp::PetBionicsApp()
    : _sensor(_config.analogPin),
      _filter(_config.filterAlpha),
      _detector(_config.eventThreshold, _config.eventCooldownMs),
      _logger(_config.sdCsPin, _config.sdPath),
      _web(_config, _status, _logger),
      _orientation(0.97f),
    _sampleCursorUs(0),
    _loggingWasActive(false),
    _status{false, false, false, false, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    _lowPowerModeActive(false),
    _latestEstimatedKg(0.0f),
    _lastEstimatedKgUpdateMs(0),
    _cmdBuffer{0},
    _cmdLen(0)
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
    loadWifiCredentials();
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
    handleSerialCommand();

    // If in sleep mode, skip main loop
    if (_lowPowerModeActive)
        return;

    const uint32_t nowMs = _clock.nowMs();
    _sensor.updateHealth(nowMs);
    _logger.updateHealth(nowMs);
    _web.update();

    // Read battery voltage every 2 seconds (silent)
    static uint32_t lastBatteryReadMs = 0;
    if ((nowMs - lastBatteryReadMs) >= 2000)
    {
        lastBatteryReadMs = nowMs;
        int adcRaw = analogRead(_config.batteryAdcPin);
        float adcVoltage = (float)adcRaw * 3.3f / 4095.0f;
        _status.batteryVoltage = adcVoltage * _config.batteryCalibration;
    }

    // Heartbeat compacto a cada 5 segundos
    static uint32_t lastHeartbeatMs = 0;
    if ((nowMs - lastHeartbeatMs) >= 5000)
    {
        lastHeartbeatMs = nowMs;
        const bool wifiOk = (WiFi.status() == WL_CONNECTED);
        if (_config.acquisitionEnabled)
        {
            Serial.printf("[OK] GRAVAR  | Batt:%.2fV | Kg:%.3f | %lu amostras %lu eventos | WiFi:%s\n",
                          _status.batteryVoltage, _status.loadCellEstimatedKg,
                          static_cast<unsigned long>(_status.samples),
                          static_cast<unsigned long>(_status.events),
                          wifiOk ? "OK" : "--");
        }
        else
        {
            Serial.printf("[OK] PARADO  | Batt:%.2fV | Kg:%.3f | IMU:%s SD:%s HX:%s | WiFi:%s\n",
                          _status.batteryVoltage, _status.loadCellEstimatedKg,
                          _status.imuReady   ? "OK" : "FAIL",
                          _status.sdReady    ? "OK" : "FAIL",
                          _status.hx711Ready ? "OK" : "FAIL",
                          wifiOk ? "OK" : "--");
        }
    }

    if (_config.acquisitionEnabled)
    {
        if (!_loggingWasActive)
        {
            startSession();
        }

        const uint32_t nowUs = micros();
        while ((nowUs - _sampleCursorUs) >= _config.samplePeriodUs)
        {
            _sampleCursorUs += _config.samplePeriodUs;
            processSample(_sampleCursorUs / 1000U, _sampleCursorUs);
        }
    }
    else
    {
        if (_loggingWasActive)
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
    _latestEstimatedKg = 0.0f;
    _lastEstimatedKgUpdateMs = 0;
    _sampleCursorUs  = micros();
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

    _loggingWasActive = true;
}

void PetBionicsApp::stopSession()
{
    _logger.stopSession();
    _loggingWasActive = false;
    Serial.printf("[Run] STOP — %lu amostras gravadas\n",
                  static_cast<unsigned long>(_status.samples));
}

void PetBionicsApp::handleSerialCommand()
{
    while (Serial.available())
    {
        char c = Serial.read();

        if (c == '\n' || c == '\r')
        {
            if (_cmdLen > 0)
            {
                _cmdBuffer[_cmdLen] = '\0';

                if (strcmp(_cmdBuffer, "help") == 0)
                {
                    Serial.println("Comandos disponiveis:");
                    Serial.println("  start               — inicia sessao de aquisicao");
                    Serial.println("  stop                — para sessao de aquisicao");
                    Serial.println("  status              — mostra estado completo");
                    Serial.println("  files               — lista ficheiros no SD");
                    Serial.println("  delete <ficheiro>   — apaga ficheiro do SD");
                    Serial.println("  delete-all          — apaga todos os ficheiros do SD");
                    Serial.println("  ssid <nome> <pass>  — muda WiFi (sem espacos no nome/pass)");
                    Serial.println("  wifi-on             — liga o WiFi");
                    Serial.println("  wifi-off            — desliga o WiFi");
                    Serial.println("  wifi-reset          — reconecta ao WiFi");
                    Serial.println("  reset               — reinicia o ESP32");
                    Serial.println("  sleep               — modo low-power");
                    Serial.println("  wakeup              — sai do modo low-power");
                }
                else if (strcmp(_cmdBuffer, "start") == 0)
                {
                    if (!_config.acquisitionEnabled)
                    {
                        _config.acquisitionEnabled = true;
                        Serial.println("[Cmd] Aquisicao iniciada");
                    }
                    else
                    {
                        Serial.println("[Cmd] Ja esta a gravar");
                    }
                }
                else if (strcmp(_cmdBuffer, "stop") == 0)
                {
                    if (_config.acquisitionEnabled)
                    {
                        _config.acquisitionEnabled = false;
                        Serial.println("[Cmd] Aquisicao parada");
                    }
                    else
                    {
                        Serial.println("[Cmd] Ja esta parado");
                    }
                }
                else if (strcmp(_cmdBuffer, "status") == 0)
                {
                    printSerialStatus();
                }
                else if (strcmp(_cmdBuffer, "files") == 0)
                {
                    printSerialFiles();
                }
                else if (strncmp(_cmdBuffer, "delete ", 7) == 0)
                {
                    const char *filePath = _cmdBuffer + 7;
                    if (_logger.deleteFile(filePath))
                        Serial.printf("[Cmd] Apagado: %s\n", filePath);
                    else
                        Serial.printf("[Cmd] Falha ao apagar: %s\n", filePath);
                }
                else if (strcmp(_cmdBuffer, "delete-all") == 0)
                {
                    Serial.println("[Cmd] A apagar todos os ficheiros...");
                    int count = _logger.deleteAllFiles();
                    Serial.printf("[Cmd] %d ficheiro(s) apagado(s)\n", count);
                }
                else if (strncmp(_cmdBuffer, "ssid ", 5) == 0)
                {
                    char *rest = _cmdBuffer + 5;
                    char *sep = strchr(rest, ' ');
                    if (!sep)
                    {
                        Serial.println("[Cmd] Uso: ssid <nome> <password>");
                    }
                    else
                    {
                        *sep = '\0';
                        strncpy(_config.wifiSsid, rest, sizeof(_config.wifiSsid) - 1);
                        _config.wifiSsid[sizeof(_config.wifiSsid) - 1] = '\0';
                        strncpy(_config.wifiPassword, sep + 1, sizeof(_config.wifiPassword) - 1);
                        _config.wifiPassword[sizeof(_config.wifiPassword) - 1] = '\0';
                        saveWifiCredentials();
                        Serial.printf("[Cmd] WiFi guardado: SSID='%s' — escreve 'wifi-reset' para aplicar\n", _config.wifiSsid);
                    }
                }
                else if (strcmp(_cmdBuffer, "wifi-on") == 0)
                {
                    if (WiFi.status() == WL_CONNECTED)
                    {
                        Serial.printf("[WiFi] Ja ligado — http://%s\n", WiFi.localIP().toString().c_str());
                    }
                    else
                    {
                        Serial.println("[WiFi] A ligar...");
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
                            Serial.printf("[WiFi] Ligado — http://%s\n", WiFi.localIP().toString().c_str());
                        else
                            Serial.println("[WiFi] Falha na ligacao");
                    }
                }
                else if (strcmp(_cmdBuffer, "wifi-off") == 0)
                {
                    WiFi.disconnect(true);
                    Serial.println("[WiFi] Desligado");
                }
                else if (strcmp(_cmdBuffer, "wifi-reset") == 0)
                {
                    Serial.println("[Cmd] A reconectar WiFi...");
                    WiFi.disconnect(true);
                    delay(300);
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
                        Serial.printf("[WiFi] Ligado — http://%s\n", WiFi.localIP().toString().c_str());
                    else
                        Serial.println("[WiFi] Falha na ligacao");
                }
                else if (strcmp(_cmdBuffer, "reset") == 0)
                {
                    Serial.println("[Cmd] A reiniciar...");
                    Serial.flush();
                    delay(100);
                    ESP.restart();
                }
                else if (strcmp(_cmdBuffer, "sleep") == 0)
                {
                    if (!_lowPowerModeActive)
                    {
                        enterLowPowerMode();
                    }
                }
                else if (strcmp(_cmdBuffer, "wakeup") == 0)
                {
                    if (_lowPowerModeActive)
                    {
                        _lowPowerModeActive = false;
                    }
                }
                else
                {
                    Serial.printf("[Cmd] Comando desconhecido: '%s' (escreve 'help')\n", _cmdBuffer);
                }

                _cmdLen = 0;
            }
        }
        else if (_cmdLen < sizeof(_cmdBuffer) - 1)
        {
            _cmdBuffer[_cmdLen++] = c;
        }
    }
}

void PetBionicsApp::enterLowPowerMode()
{
    _lowPowerModeActive = true;

    // Stop logging if active
    if (_loggingWasActive)
    {
        _logger.stopSession();
        _loggingWasActive = false;
    }

    // Disconnect WiFi to save power
    WiFi.disconnect(true);  // true = turn off WiFi radio
    Serial.println("[Sleep] Entering sleep mode... send 'wakeup' to resume");
    Serial.println("[Sleep] WiFi disabled, low-power mode active");
    Serial.flush();

    // Limpar buffer de série e estado do comando para evitar bytes residuais
    // da reconexão USB CDC que causariam comandos concatenados (ex: "sleepwakeup")
    while (Serial.available()) Serial.read();
    _cmdLen = 0;
    _cmdBuffer[0] = '\0';

    // Loop in sleep mode until wakeup received
    while (_lowPowerModeActive)
    {
        delay(100);  // Check serial every 100ms
        handleSerialCommand();  // This will set _lowPowerModeActive = false on "wakeup"
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

void PetBionicsApp::processSample(uint32_t nowMs, uint32_t nowUs)
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

    // Update the calibrated HX711 weight more slowly than the main sampling loop
    // so the UI gets a real kg value without stalling the acquisition rate.
    if ((nowMs - _lastEstimatedKgUpdateMs) >= 250)
    {
        float estimatedKg = _sensor.readEstimatedWeightKg();
        if (!isnan(estimatedKg))
        {
            _latestEstimatedKg = estimatedKg;
            _lastEstimatedKgUpdateMs = nowMs;
        }
    }

    const float       dtSeconds = static_cast<float>(_config.samplePeriodUs) / 1000000.0f;
    const Orientation orient    = _orientation.update(ax, ay, az, gx, gy, gz, mx, my, mz, dtSeconds);

    RawSample sample{nowUs, raw, _latestEstimatedKg, _status.batteryVoltage,
                     ax, ay, az, gx, gy, gz, mx, my, mz,
                     orient.roll, orient.pitch, orient.yaw};
    _logger.append(sample, event);

    _status.loadCellEstimatedKg = _latestEstimatedKg;
    _status.roll = orient.roll;
    _status.pitch = orient.pitch;
    _status.yaw = orient.yaw;

    _status.samples++;
    if (event.triggered) { _status.events++; }
}

void PetBionicsApp::printSerialStatus() const
{
    const char *battClass = _status.batteryVoltage < 3.0f ? "BAIXA"
                          : _status.batteryVoltage < 3.5f ? "OK-"
                          : "OK";
    Serial.println("=== petBionic status ===");
    Serial.printf("Estado:   %s\n", _config.acquisitionEnabled ? "A gravar" : "Parado");
    Serial.printf("Bateria:  %.2f V  [%s]\n", _status.batteryVoltage, battClass);
    Serial.printf("Kg:       %.3f kg\n", _status.loadCellEstimatedKg);
    Serial.printf("Roll:     %.2f graus\n", _status.roll);
    Serial.printf("Pitch:    %.2f graus\n", _status.pitch);
    Serial.printf("Yaw:      %.2f graus\n", _status.yaw);
    Serial.printf("SD Card:  %s\n", _status.sdReady    ? "OK" : "FALHA");
    Serial.printf("IMU:      %s\n", _status.imuReady   ? "OK" : "FALHA");
    Serial.printf("HX711:    %s\n", _status.hx711Ready ? "OK" : "FALHA");
    Serial.printf("Amostras: %lu\n", static_cast<unsigned long>(_status.samples));
    Serial.printf("Eventos:  %lu\n", static_cast<unsigned long>(_status.events));
    Serial.println("========================");
}

void PetBionicsApp::loadWifiCredentials()
{
    Preferences prefs;
    prefs.begin("wifi", true);
    if (prefs.isKey("ssid"))
    {
        String ssid = prefs.getString("ssid", "");
        String pass = prefs.getString("pass", "");
        strncpy(_config.wifiSsid, ssid.c_str(), sizeof(_config.wifiSsid) - 1);
        _config.wifiSsid[sizeof(_config.wifiSsid) - 1] = '\0';
        strncpy(_config.wifiPassword, pass.c_str(), sizeof(_config.wifiPassword) - 1);
        _config.wifiPassword[sizeof(_config.wifiPassword) - 1] = '\0';
        Serial.printf("[Init] WiFi carregado da NVS: SSID='%s'\n", _config.wifiSsid);
    }
    prefs.end();
}

void PetBionicsApp::saveWifiCredentials()
{
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString("ssid", _config.wifiSsid);
    prefs.putString("pass", _config.wifiPassword);
    prefs.end();
}

void PetBionicsApp::printSerialFiles()
{
    const int kMaxFiles = 50;
    String filePaths[kMaxFiles];
    int fileCount = _logger.listFiles(filePaths, kMaxFiles);

    Serial.println("=== Ficheiros SD ===");
    if (fileCount == 0)
    {
        Serial.println("  (sem ficheiros)");
    }
    else
    {
        for (int i = 0; i < fileCount; i++)
        {
            size_t sizeBytes = _logger.getFileSize(filePaths[i].c_str());
            Serial.printf("  %-40s  %4u KB\n", filePaths[i].c_str(), (unsigned)(sizeBytes / 1024));
        }
        Serial.printf("(%d ficheiro%s)\n", fileCount, fileCount == 1 ? "" : "s");
    }
    Serial.println("====================");
}
