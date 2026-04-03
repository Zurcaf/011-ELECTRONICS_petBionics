#pragma once

#include <Arduino.h>

#include "../core/AppConfig.h"
#include "../core/AppTypes.h"

class BleControl
{
public:
  using CustomCommandHandler = bool (*)(void *ctx, const String &cmd, String &ack);

  explicit BleControl(AppConfig &config);
  void begin(const char *deviceName = "PetBionic");
  void updateStatus(const AppStatus &status, uint32_t nowMs);
  void applyCommand(const String &cmd);
  uint64_t currentEpochMs(uint32_t nowMs) const;
  void setCustomCommandHandler(CustomCommandHandler handler, void *context);

private:
  AppConfig &_config;
  uint32_t _lastStatusMs;
  String _statusCache;
  AppStatus _lastStatusSnapshot;
  AppStatus _lastPublishedStatus;
  bool _hasPublishedStatus;
  bool _hasStatusSnapshot;
  String _pendingAck;
  bool tryApplyTimeCommand(const String &cmd);
  void publishStatus(const AppStatus &status, uint32_t nowMs, bool force);
  void acknowledgeCommand(const char *ack, uint32_t nowMs);
  uint64_t nowEpochMs(uint32_t nowMs) const;
  bool _timeSyncRequested;
  uint32_t _lastTimeSetMs;
  bool _timeSynced;
  int64_t _epochOffsetMs;
  CustomCommandHandler _customCommandHandler;
  void *_customCommandContext;
};
