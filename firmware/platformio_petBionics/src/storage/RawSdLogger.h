#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SPI.h>

#include "../core/AppTypes.h"

class RawSdLogger
{
public:
  RawSdLogger(uint8_t csPin, const char *filePath, const char *pendingRootPath = "/local_files_only");
  bool begin();
  bool startSession(uint64_t startEpochMs);
  void stopSession();
  void updateHealth(uint32_t nowMs);
  bool isReady() const { return _ready; }
  bool append(const RawSample &sample, const EventInfo &event);
  const char *activeSessionPath() const { return _activeFilePath; }
  const char *lastSessionPath() const { return _lastSessionFilePath; }

private:
  uint8_t _csPin;
  const char *_filePath;
  const char *_pendingRootPath;
  bool _ready;
  SPIClass _spi;
  uint32_t _lastHealthCheckMs;
  bool _sessionOpen;
  uint64_t _sessionStartEpochMs;
  char _activeFilePath[96];
  char _lastSessionFilePath[96];
  bool ensureHeader(const char *path, uint64_t startEpochMs);
};
