#include "BleControl.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/time.h>

#if __has_include(<BLEDevice.h>)
#include <BLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#define PETBIONICS_HAS_BLE 1
#else
#define PETBIONICS_HAS_BLE 0
#endif

#ifndef PETBIONICS_BLE_DEBUG
#define PETBIONICS_BLE_DEBUG 0
#endif

#if PETBIONICS_BLE_DEBUG
#define BLE_DEBUG_PRINTLN(msg) Serial.println(msg)
#define BLE_DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define BLE_DEBUG_PRINTLN(msg) ((void)0)
#define BLE_DEBUG_PRINTF(...) ((void)0)
#endif

namespace
{
#if PETBIONICS_HAS_BLE
  const char *kServiceUuid = "14f16000-9d9c-470f-9f6a-6e6fe401a001";
  const char *kControlUuid = "14f16001-9d9c-470f-9f6a-6e6fe401a001";
  const char *kStatusUuid = "14f16002-9d9c-470f-9f6a-6e6fe401a001";

  BLEAdvertising *g_advertising = nullptr;
  BLECharacteristic *g_controlCharacteristic = nullptr;
  BLECharacteristic *g_statusCharacteristic = nullptr;
  BleControl *g_instance = nullptr;

  class ServerCallbacks : public BLEServerCallbacks
  {
    void onDisconnect(BLEServer *server) override
    {
      (void)server;
      if (g_advertising)
      {
        g_advertising->start();
      }
    }
  };

  class ControlCallbacks : public BLECharacteristicCallbacks
  {
    void onWrite(BLECharacteristic *pCharacteristic) override
    {
      if (!g_instance || !pCharacteristic)
      {
        return;
      }

      std::string value = pCharacteristic->getValue();
      if (value.empty())
      {
        BLE_DEBUG_PRINTLN("[BLE RX] empty write payload");
        return;
      }

      String command = String(value.c_str());
      BLE_DEBUG_PRINTF("[BLE RX] raw='%s' len=%u\n", command.c_str(), static_cast<unsigned>(command.length()));
      command.trim();
      if (command.length() == 0)
      {
        BLE_DEBUG_PRINTLN("[BLE RX] payload became empty after trim");
        return;
      }

      BLE_DEBUG_PRINTF("[BLE RX] cmd='%s'\n", command.c_str());
      g_instance->applyCommand(command);
    }
  };
#endif
} // namespace

BleControl::BleControl(AppConfig &config)
    : _config(config),
      _lastStatusMs(0),
      _statusCache("{}"),
      _lastStatusSnapshot{false, false, false, false, false, false, 0, 0},
      _lastPublishedStatus{false, false, false, false, false, false, 0, 0},
      _hasPublishedStatus(false),
      _hasStatusSnapshot(false),
      _pendingAck(""),
      _timeSyncRequested(true),
      _lastTimeSetMs(0),
      _timeSynced(false),
      _epochOffsetMs(0),
      _customCommandHandler(nullptr),
      _customCommandContext(nullptr) {}

void BleControl::setCustomCommandHandler(CustomCommandHandler handler, void *context)
{
  _customCommandHandler = handler;
  _customCommandContext = context;
}

void BleControl::acknowledgeCommand(const char *ack, uint32_t nowMs)
{
  if (ack && *ack)
  {
    _pendingAck = ack;
  }

  if (_hasStatusSnapshot)
  {
    AppStatus statusNow = _lastStatusSnapshot;
    statusNow.acquisitionEnabled = _config.acquisitionEnabled;
    publishStatus(statusNow, nowMs, true);
    return;
  }

  _lastStatusMs = nowMs - 1000U;
}

uint64_t BleControl::currentEpochMs(uint32_t nowMs) const
{
  return nowEpochMs(nowMs);
}

bool BleControl::tryApplyTimeCommand(const String &cmd)
{
  if (!cmd.startsWith("TIME="))
  {
    return false;
  }

  String rawValue = cmd.substring(5);
  rawValue.trim();
  if (rawValue.length() == 0)
  {
    BLE_DEBUG_PRINTLN("[BLE RX] TIME ignored: missing value");
    return true;
  }

  char *endPtr = nullptr;
  long long parsed = strtoll(rawValue.c_str(), &endPtr, 10);
  if (endPtr == rawValue.c_str() || (endPtr && *endPtr != '\0') || parsed <= 0)
  {
    BLE_DEBUG_PRINTF("[BLE RX] TIME ignored: invalid value '%s'\n", rawValue.c_str());
    return true;
  }

  uint64_t epochValue = static_cast<uint64_t>(parsed);
  const uint64_t kMillisThreshold = 100000000000ULL;
  uint64_t epochMs = epochValue;
  if (epochValue < kMillisThreshold)
  {
    epochMs = epochValue * 1000ULL;
  }

  struct timeval tv;
  tv.tv_sec = static_cast<time_t>(epochMs / 1000ULL);
  tv.tv_usec = static_cast<suseconds_t>((epochMs % 1000ULL) * 1000ULL);
  if (settimeofday(&tv, nullptr) != 0)
  {
    BLE_DEBUG_PRINTLN("[BLE RX] TIME warning: settimeofday failed, using offset fallback");
  }

  _epochOffsetMs = static_cast<int64_t>(epochMs) - static_cast<int64_t>(millis());
  _timeSynced = true;
  _timeSyncRequested = false;
  _lastTimeSetMs = millis();
  BLE_DEBUG_PRINTF("[BLE RX] TIME applied epoch_ms=%llu\n", static_cast<unsigned long long>(epochMs));
  return true;
}

uint64_t BleControl::nowEpochMs(uint32_t nowMs) const
{
  if (!_timeSynced)
  {
    return 0;
  }

  struct timeval tv;
  if (gettimeofday(&tv, nullptr) == 0)
  {
    if (tv.tv_sec < 0 || tv.tv_usec < 0)
    {
      return 0;
    }

    return static_cast<uint64_t>(tv.tv_sec) * 1000ULL +
           static_cast<uint64_t>(tv.tv_usec) / 1000ULL;
  }

  int64_t shifted = static_cast<int64_t>(nowMs) + _epochOffsetMs;
  if (shifted < 0)
  {
    return 0;
  }

  return static_cast<uint64_t>(shifted);
}

void BleControl::begin(const char *deviceName)
{
#if PETBIONICS_HAS_BLE
  BLEDevice::init(deviceName);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(kServiceUuid);

  g_controlCharacteristic = service->createCharacteristic(
      kControlUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  g_controlCharacteristic->setCallbacks(new ControlCallbacks());

  g_statusCharacteristic = service->createCharacteristic(
      kStatusUuid, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  g_statusCharacteristic->addDescriptor(new BLE2902());
  g_statusCharacteristic->setValue(_statusCache.c_str());

  service->start();
  g_advertising = BLEDevice::getAdvertising();
  g_advertising->addServiceUUID(kServiceUuid);
  g_advertising->start();

  g_instance = this;
#else
  (void)deviceName;
  Serial.println("BLE headers not found, BLE control disabled at compile time");
#endif
}

void BleControl::publishStatus(const AppStatus &status, uint32_t nowMs, bool force)
{
#if PETBIONICS_HAS_BLE
  const uint32_t kStatusPeriodMs = 400;

  if (!g_statusCharacteristic)
  {
    return;
  }

  if (!force && (nowMs - _lastStatusMs) < kStatusPeriodMs)
  {
    return;
  }

  const uint32_t kPeriodicResyncMs = 60000;
  if (_timeSynced && (nowMs - _lastTimeSetMs) >= kPeriodicResyncMs)
  {
    _timeSyncRequested = true;
  }

  uint64_t epochMs = nowEpochMs(nowMs);
  char epochMsBuffer[24];
  snprintf(epochMsBuffer, sizeof(epochMsBuffer), "%llu", static_cast<unsigned long long>(epochMs));

  _statusCache = String("{") +
                 "\"acq\":" + (status.acquisitionEnabled ? "true" : "false") + "," +
                 "\"sd\":" + (status.sdReady ? "true" : "false") + "," +
                 "\"imu\":" + (status.imuReady ? "true" : "false") + "," +
                 "\"hx711\":" + (status.hx711Ready ? "true" : "false") + "," +
                 "\"wifi\":" + (status.wifiConnected ? "true" : "false") + "," +
                 "\"cloud_cfg\":" + (status.cloudConfigured ? "true" : "false") + "," +
                 "\"samples\":" + String(status.samples) + "," +
                 "\"events\":" + String(status.events) + "," +
                 "\"uptime_ms\":" + String(nowMs) + "," +
                 "\"time_sync_needed\":" + (_timeSyncRequested ? "true" : "false") + "," +
                 "\"time_synced\":" + (_timeSynced ? "true" : "false") + "," +
                 "\"time_ms\":" + String(epochMsBuffer);

  if (_pendingAck.length() > 0)
  {
    _statusCache += ",\"cmd_ack\":\"" + _pendingAck + "\"";
  }

  _statusCache += "}";

  g_statusCharacteristic->setValue(_statusCache.c_str());
  g_statusCharacteristic->notify();
  _lastStatusMs = nowMs;
  _lastPublishedStatus = status;
  _hasPublishedStatus = true;
  _pendingAck = "";
#else
  (void)status;
  (void)nowMs;
  (void)force;
#endif
}

void BleControl::updateStatus(const AppStatus &status, uint32_t nowMs)
{
  _lastStatusSnapshot = status;
  _hasStatusSnapshot = true;

  const bool importantChange = !_hasPublishedStatus ||
                               status.acquisitionEnabled != _lastPublishedStatus.acquisitionEnabled ||
                               status.sdReady != _lastPublishedStatus.sdReady ||
                               status.imuReady != _lastPublishedStatus.imuReady ||
                               status.hx711Ready != _lastPublishedStatus.hx711Ready ||
                               status.wifiConnected != _lastPublishedStatus.wifiConnected ||
                               status.cloudConfigured != _lastPublishedStatus.cloudConfigured;

  publishStatus(status, nowMs, importantChange);
}

void BleControl::applyCommand(const String &cmd)
{
  const uint32_t nowMs = millis();
  const uint64_t epochMsBefore = nowEpochMs(nowMs);
  if (epochMsBefore > 0)
  {
    Serial.printf("BLE RX cmd='%s' t_real_ms=%llu\n",
                  cmd.c_str(),
                  static_cast<unsigned long long>(epochMsBefore));
  }
  else
  {
    Serial.printf("BLE RX cmd='%s' t_real_ms=UNSYNCED\n", cmd.c_str());
  }

  if (cmd.equalsIgnoreCase("TIME_SYNC_NOW"))
  {
    _timeSyncRequested = true;
    acknowledgeCommand("TIME_SYNC_NOW", nowMs);
    BLE_DEBUG_PRINTLN("[BLE RX] TIME_SYNC_NOW accepted");
    return;
  }

  if (tryApplyTimeCommand(cmd))
  {
    acknowledgeCommand("TIME", nowMs);
    return;
  }

  if (cmd.equalsIgnoreCase("START"))
  {
    _config.acquisitionEnabled = true;
    acknowledgeCommand("START", nowMs);
    const uint64_t epochMsAfter = nowEpochMs(nowMs);
    if (epochMsAfter > 0)
    {
      Serial.printf("BLE START -> logging ON t_real_ms=%llu\n",
                    static_cast<unsigned long long>(epochMsAfter));
    }
    else
    {
      Serial.println("BLE START -> logging ON t_real_ms=UNSYNCED");
    }
    BLE_DEBUG_PRINTLN("[BLE RX] START accepted");
    return;
  }

  if (cmd.equalsIgnoreCase("STOP"))
  {
    _config.acquisitionEnabled = false;
    acknowledgeCommand("STOP", nowMs);
    const uint64_t epochMsAfter = nowEpochMs(nowMs);
    if (epochMsAfter > 0)
    {
      Serial.printf("BLE STOP -> logging OFF t_real_ms=%llu\n",
                    static_cast<unsigned long long>(epochMsAfter));
    }
    else
    {
      Serial.println("BLE STOP -> logging OFF t_real_ms=UNSYNCED");
    }
    BLE_DEBUG_PRINTLN("[BLE RX] STOP accepted");
    return;
  }

  if (cmd.startsWith("ALPHA="))
  {
    float v = cmd.substring(6).toFloat();
    if (v >= 0.0f && v <= 1.0f)
    {
      _config.filterAlpha = v;
      acknowledgeCommand("ALPHA", nowMs);
      BLE_DEBUG_PRINTF("[BLE RX] ALPHA applied %.3f\n", v);
    }
    else
    {
      BLE_DEBUG_PRINTF("[BLE RX] ALPHA ignored %.3f (out of range)\n", v);
    }
    return;
  }

  if (cmd.startsWith("THR="))
  {
    float v = cmd.substring(4).toFloat();
    if (v >= 0.0f)
    {
      _config.eventThreshold = v;
      acknowledgeCommand("THR", nowMs);
      BLE_DEBUG_PRINTF("[BLE RX] THR applied %.3f\n", v);
    }
    else
    {
      BLE_DEBUG_PRINTF("[BLE RX] THR ignored %.3f (negative)\n", v);
    }
    return;
  }

  if (cmd.startsWith("PERIOD="))
  {
    long v = cmd.substring(7).toInt();
    if (v >= 1)
    {
      _config.samplePeriodUs = static_cast<uint32_t>(v) * 1000UL;
      acknowledgeCommand("PERIOD", nowMs);
      BLE_DEBUG_PRINTF("[BLE RX] PERIOD applied %ld ms\n", v);
    }
    else
    {
      BLE_DEBUG_PRINTF("[BLE RX] PERIOD ignored %ld (must be >=1)\n", v);
    }
    return;
  }

  if (_customCommandHandler)
  {
    String ack;
    if (_customCommandHandler(_customCommandContext, cmd, ack))
    {
      if (ack.length() == 0)
      {
        ack = "CUSTOM";
      }
      acknowledgeCommand(ack.c_str(), nowMs);
      return;
    }
  }

  BLE_DEBUG_PRINTF("[BLE RX] unknown cmd '%s'\n", cmd.c_str());
}
