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
  bool isReady() const { return _sdCardReady; }
  bool append(const RawSample &sample, const EventInfo &event);
  const char *activeFilePath() const { return _sessionFilePath; }

  // File listing for download interface
  int listFiles(String fileList[], int maxFiles) const;
  size_t getFileSize(const char *filename) const;
  bool readFile(const char *filename, uint8_t *buffer, size_t bufferSize, size_t &bytesRead);
  bool readFileAt(const char *filename, size_t offset, uint8_t *buffer, size_t bufferSize, size_t &bytesRead);

  // File deletion
  bool deleteFile(const char *path);
  int  deleteAllFiles();

private:
  // Keep the file open and flush once per second to reduce SD overhead.
  static constexpr uint16_t kSamplesPerFlush = 80;
  static constexpr const char *kInboxRootFolder = "/inbox";

  uint8_t _csPin;
  const char *_logBasePath;
  bool _sdCardReady;
  SPIClass _spi;
  uint32_t _lastHealthCheckMs;
  bool _sessionIsOpen;
  uint64_t _sessionStartEpochMs;
  char _sessionFilePath[96];
  File _activeFile; // kept open for the duration of a session
  uint16_t _samplesSinceLastFlush;

  // Last written line (simple dedupe guard to avoid accidental repeated rows)
  char _lastLine[320];
  bool _hasLastLine;
  // Last written sample timestamp (micros) to avoid duplicate sampleUs writes
  uint32_t _lastSampleUs;
  bool _hasLastSampleUs;

  bool ensureHeader(const char *path);
};
