#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

#include "../core/AppTypes.h"

class RawSdLogger
{
public:
  RawSdLogger(uint8_t csPin, const char *filePath);
  bool begin();
  bool startSession(uint64_t startEpochMs);
  void stopSession();
  void updateHealth(uint32_t nowMs);
  bool isReady() const { return _ready; }
  bool append(const RawSample &sample, const EventInfo &event);
  const char *activeFilePath() const { return _activeFilePath; }

  // Call after a successful Firebase upload to move the file from inbox/ to sent/.
  // Returns true if the file was moved successfully.
  bool markAsSent(const char *filePath);

private:
  static constexpr uint16_t kFlushEveryN = 80;          // flush once per second at 80 Hz
  static constexpr const char *kInboxFolder = "/inbox"; // runs waiting to be uploaded
  static constexpr const char *kSentFolder  = "/sent";  // runs already sent to Firebase

  uint8_t _csPin;
  const char *_filePath;
  bool _ready;
  SPIClass _spi;
  uint32_t _lastHealthCheckMs;
  bool _sessionOpen;
  uint64_t _sessionStartEpochMs;
  char _activeFilePath[96];
  File _activeFile;       // kept open for the duration of a session
  uint16_t _flushCounter;

  bool ensureHeader(const char *path, uint64_t startEpochMs);
};
