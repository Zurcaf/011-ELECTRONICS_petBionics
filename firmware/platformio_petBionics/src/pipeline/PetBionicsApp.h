#pragma once

#include <Arduino.h>

#include "../ble/BleControl.h"
#include "../core/AppConfig.h"
#include "../core/AppTypes.h"
#include "../core/LocalClock.h"
#include "../sensors/RawSensor.h"
#include "../storage/RawSdLogger.h"
#include "LightFilter.h"
#include "SimpleEventDetector.h"
#include "../network/WifiManager.h"
#include "../cloud/FirestoreSync.h"

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
  RawSdLogger _logger;
  BleControl _ble;
  WifiManager _wifi;
  FirestoreSync _cloud;

  String _pendingWifiSsid;
  String _pendingWifiPass;

  uint32_t _lastSampleUs;
  bool _wasAcquiring;
  AppStatus _status;

  void sampleStep(uint32_t nowMs, uint32_t nowUs);
  static bool customBleCommandBridge(void *ctx, const String &cmd, String &ack);
  bool handleCustomBleCommand(const String &cmd, String &ack);
};
