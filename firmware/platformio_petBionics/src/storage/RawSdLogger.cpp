#include "RawSdLogger.h"

#include <SD.h>
#include <SPI.h>
#include <time.h>

#include "../core/Pinout.h"

namespace
{
  // Pin the SD timestamps to the local timezone used by the device.
  void configureLocalTimezone()
  {
    setenv("TZ", "WET0WEST,M3.5.0/1,M10.5.0/2", 1);
    tzset();
  }

  // If device localtime appears offset, adjust printed times by this many hours.
  // Set to 0 for no adjustment; set to 1 if localtime is 1 hour behind.
  static constexpr int kLocalTimeOffsetHours = 1;

  void copyBaseNameWithoutExtension(const char *path, char *out, size_t outSize);

  void stripLeadingSlash(char *baseName)
  {
    if (!baseName)
    {
      return;
    }
    if (baseName[0] == '/')
    {
      memmove(baseName, baseName + 1, strlen(baseName));
    }
  }

  bool hasCsvExtension(const char *name)
  {
    if (!name)
    {
      return false;
    }
    const char *dot = strrchr(name, '.');
    return dot && strcmp(dot, ".csv") == 0;
  }

  bool formatDateKey(uint64_t epochMs, char *out, size_t outSize)
  {
    if (!out || outSize < 9 || epochMs == 0)
    {
      return false;
    }
    time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
    // Apply optional local offset
    seconds += static_cast<time_t>(kLocalTimeOffsetHours) * 3600;
    struct tm localTm;
    localtime_r(&seconds, &localTm);
    snprintf(out,
             outSize,
             "%04d%02d%02d",
             localTm.tm_year + 1900,
             localTm.tm_mon + 1,
             localTm.tm_mday);
    return true;
  }

  void formatSessionDayFolder(uint64_t epochMs, char *out, size_t outSize)
  {
    if (!out || outSize == 0)
    {
      return;
    }

    char dateKey[9] = {0};
    if (!formatDateKey(epochMs, dateKey, sizeof(dateKey)))
    {
      snprintf(out, outSize, "/inbox/unsynced");
      return;
    }

    snprintf(out, outSize, "/inbox/%s", dateKey);
  }

  uint16_t countCsvFilesInFolder(const char *dayFolder)
  {
    if (!dayFolder)
    {
      return 0;
    }

    uint16_t csvCount = 0;

    File directory = SD.open(dayFolder);
    if (!directory)
    {
      return 0;
    }

    File entry = directory.openNextFile();
    while (entry)
    {
      if (!entry.isDirectory())
      {
        char entryName[96];
        strncpy(entryName, entry.name(), sizeof(entryName) - 1);
        entryName[sizeof(entryName) - 1] = '\0';

        if (hasCsvExtension(entryName))
        {
          if (csvCount < 65535)
          {
            ++csvCount;
          }
        }
      }

      entry.close();
      entry = directory.openNextFile();
    }
    directory.close();

    return csvCount;
  }

  uint16_t calculateNextRunNumber(const char *basePath, const char *dayFolder, uint64_t epochMs)
  {
    if (!basePath || !dayFolder || epochMs == 0)
    {
      return 0;
    }

    const uint16_t count = countCsvFilesInFolder(dayFolder);
    return static_cast<uint16_t>(count + 1U);
  }

  bool isCardPresent()
  {
    return SD.cardType() != CARD_NONE;
  }

  bool canAccessCurrentSessionPath(bool sessionIsOpen, const char *sessionFilePath)
  {
    File probe = sessionIsOpen
                     ? SD.open(sessionFilePath, FILE_APPEND)
                     : SD.open("/", FILE_READ);
    if (!probe)
    {
      return false;
    }
    probe.close();
    return true;
  }

  void copyBaseNameWithoutExtension(const char *path, char *out, size_t outSize)
  {
    if (!path || !out || outSize == 0)
    {
      return;
    }

    size_t len = strnlen(path, outSize - 1);
    strncpy(out, path, len);
    out[len] = '\0';

    const char *dot = strrchr(out, '.');
    if (dot && strcmp(dot, ".csv") == 0)
    {
      out[dot - out] = '\0';
    }
  }

  void formatSessionFilePath(const char *basePath, const char *dayFolder, uint64_t epochMs, uint16_t runNumber, char *out, size_t outSize)
  {
    if (!basePath || !dayFolder || !out || outSize == 0)
    {
      return;
    }

    char base[64] = {0};
    copyBaseNameWithoutExtension(basePath, base, sizeof(base));
    if (base[0] == '\0')
    {
      strncpy(base, "/raw_log", sizeof(base) - 1);
    }
    stripLeadingSlash(base);

    if (epochMs > 0)
    {
      time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
      // Apply optional local offset so filenames reflect local day/time
      seconds += static_cast<time_t>(kLocalTimeOffsetHours) * 3600;
      struct tm localTm;
      localtime_r(&seconds, &localTm);
      snprintf(out,
               outSize,
               "%s/run%03u_%04d%02d%02d_%02d%02d%02d.csv",
               dayFolder,
               static_cast<unsigned>(runNumber),
               localTm.tm_year + 1900,
               localTm.tm_mon + 1,
               localTm.tm_mday,
               localTm.tm_hour,
               localTm.tm_min,
               localTm.tm_sec);
      return;
    }

    // No time sync – place file inside the /unsynced folder so it doesn't
    // clutter the SD root and is easy to identify later.
    snprintf(out,
             outSize,
             "%s/run%03u_unsynced_%010lu.csv",
             dayFolder,
             static_cast<unsigned>(runNumber),
             static_cast<unsigned long>(millis()));
  }

} // namespace

RawSdLogger::RawSdLogger(uint8_t csPin, const char *filePath)
    : _csPin(csPin),
      _logBasePath(filePath),
      _sdCardReady(false),
      _spi(FSPI),
      _lastHealthCheckMs(0),
      _sessionIsOpen(false),
      _sessionStartEpochMs(0),
      _samplesSinceLastFlush(0),
      _hasLastLine(false),
      _hasLastSampleUs(false)
{
  _sessionFilePath[0] = '\0';
  _lastLine[0] = '\0';
  _lastSampleUs = 0;
}

bool RawSdLogger::begin()
{
  configureLocalTimezone();
  _spi.begin(PetBionicsPinout::kSpiSck,
             PetBionicsPinout::kSpiMiso,
             PetBionicsPinout::kSpiMosi);

  _sdCardReady = SD.begin(_csPin, _spi);
  if (!_sdCardReady)
  {
    Serial.println("SD.begin failed: card not detected or wiring/CS is wrong");
    return false;
  }

  _lastHealthCheckMs = millis();
  return true;
}

bool RawSdLogger::startSession(uint64_t startEpochMs)
{
  updateHealth(millis());
  if (!_sdCardReady)
  {
    Serial.println("SD session start failed: logger not ready");
    return false;
  }

  // Close any existing session first
  if (_sessionIsOpen)
  {
    stopSession();
  }

  char dayFolder[32] = {0};
  formatSessionDayFolder(startEpochMs, dayFolder, sizeof(dayFolder));

  if (dayFolder[0] == '\0')
  {
    Serial.println("SD session start failed: invalid day folder");
    return false;
  }

  // Ensure /inbox exists before creating the day subfolder (SD.mkdir is not recursive).
  if (!SD.exists(kInboxRootFolder) && !SD.mkdir(kInboxRootFolder))
  {
    Serial.println("SD session start failed: could not create inbox folder");
    return false;
  }
  if (!SD.exists(dayFolder) && !SD.mkdir(dayFolder))
  {
    Serial.println("SD session start failed: could not create day folder");
    return false;
  }

  uint16_t runNumber = calculateNextRunNumber(_logBasePath, dayFolder, startEpochMs);
  if (runNumber == 0)
  {
    runNumber = 1;
  }

  formatSessionFilePath(_logBasePath, dayFolder, startEpochMs, runNumber, _sessionFilePath, sizeof(_sessionFilePath));
  _sessionIsOpen = true;
  _sessionStartEpochMs = startEpochMs;

  if (!ensureHeader(_sessionFilePath))
  {
    _sessionIsOpen = false;
    _sessionStartEpochMs = 0;
    _sessionFilePath[0] = '\0';
    return false;
  }

  // Open once and keep the handle open for the whole session so that each
  // append() only writes – no per-sample open/close overhead on the SD.
  _activeFile = SD.open(_sessionFilePath, FILE_APPEND);
  if (!_activeFile)
  {
    _sdCardReady = false;
    _sessionIsOpen = false;
    _sessionStartEpochMs = 0;
    _sessionFilePath[0] = '\0';
    Serial.println("SD session start failed: could not open file for append");
    return false;
  }
  _samplesSinceLastFlush = 0;
  _hasLastLine = false;
  _lastLine[0] = '\0';
  _hasLastSampleUs = false;
  _lastSampleUs = 0;

  Serial.printf("SD session file: %s\n", _sessionFilePath);
  return true;
}

void RawSdLogger::stopSession()
{
  if (_activeFile)
  {
    _activeFile.flush();
    _activeFile.close();
  }
  _sessionIsOpen = false;
  _sessionStartEpochMs = 0;
  _sessionFilePath[0] = '\0';
  _samplesSinceLastFlush = 0;
  _hasLastLine = false;
  _lastLine[0] = '\0';
  _hasLastSampleUs = false;
  _lastSampleUs = 0;
}

void RawSdLogger::updateHealth(uint32_t nowMs)
{
  const uint32_t kHealthCheckPeriodMs = 1000;
  if ((nowMs - _lastHealthCheckMs) < kHealthCheckPeriodMs)
  {
    return;
  }
  _lastHealthCheckMs = nowMs;

  if (_sdCardReady)
  {
    if (!isCardPresent() || !canAccessCurrentSessionPath(_sessionIsOpen, _sessionFilePath))
    {
      _sdCardReady = false;
      Serial.println("SD health check failed: card/file unavailable");
      return;
    }
    return;
  }

  // Attempt recovery if the card was reinserted.
  if (SD.begin(_csPin, _spi))
  {
    if (!isCardPresent())
    {
      _sdCardReady = false;
      return;
    }

    _sdCardReady = canAccessCurrentSessionPath(_sessionIsOpen, _sessionFilePath);
    if (_sdCardReady && _sessionIsOpen)
    {
      _sdCardReady = ensureHeader(_sessionFilePath);
    }
    if (_sdCardReady)
    {
      Serial.println("SD logger recovered");
    }
  }
}

bool RawSdLogger::ensureHeader(const char *path)
{
  if (!path || path[0] == '\0')
  {
    _sdCardReady = false;
    Serial.println("SD header create failed: empty file path");
    return false;
  }

  File file = SD.open(path, FILE_READ);
  if (file)
  {
    file.close();
    return true;
  }

  file = SD.open(path, FILE_WRITE);
  if (!file)
  {
    _sdCardReady = false;
    Serial.println("SD header create failed: could not open log file");
    return false;
  }

  file.println("sample_us,time_of_day,load_cell_raw,load_cell_est_kg,imu_ax,imu_ay,imu_az,imu_gx,imu_gy,imu_gz,imu_mx,imu_my,imu_mz,roll_deg,pitch_deg,yaw_deg");
  file.close();
  return true;
}

bool RawSdLogger::append(const RawSample &sample, const EventInfo &event)
{
  (void)event;

  if (!_sdCardReady || !_sessionIsOpen || !_activeFile)
  {
    return false;
  }

  // Build local time-of-day string (HH:MM:SS.mmm)
  char timeBuf[16] = {0};
  time_t nowSec = time(nullptr);
  // Apply optional offset hours to get correct local time
  nowSec += static_cast<time_t>(kLocalTimeOffsetHours) * 3600;
  struct tm localTm;
  localtime_r(&nowSec, &localTm);
  int ms = static_cast<int>(millis() % 1000);
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d.%03d", localTm.tm_hour, localTm.tm_min, localTm.tm_sec, ms);

  char line[420];
  int written = snprintf(line, sizeof(line),
                         "%lu,%s,%ld,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f\n",
                         static_cast<unsigned long>(sample.sampleUs),
                         timeBuf,
                         static_cast<long>(sample.loadCellRaw),
                         sample.loadCellEstimatedKg,
                         static_cast<int>(sample.ax),
                         static_cast<int>(sample.ay),
                         static_cast<int>(sample.az),
                         static_cast<int>(sample.gx),
                         static_cast<int>(sample.gy),
                         static_cast<int>(sample.gz),
                         static_cast<int>(sample.mx),
                         static_cast<int>(sample.my),
                         static_cast<int>(sample.mz),
                         sample.roll,
                         sample.pitch,
                         sample.yaw);
  if (written <= 0 || written >= static_cast<int>(sizeof(line)))
  {
    Serial.println("SD append failed: line formatting error");
    return false;
  }

  // Deduplicate: if the formatted line equals the last one written, skip.
  // Deduplicate by sample timestamp first (fast integer compare)
  if (_hasLastSampleUs && static_cast<uint32_t>(sample.sampleUs) == _lastSampleUs)
  {
    return true; // already written this sample
  }

  // Deduplicate fallback: if the formatted line equals the last one written, skip.
  if (_hasLastLine && strcmp(line, _lastLine) == 0)
  {
    return true; // already written
  }

  size_t bytesWritten = _activeFile.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(written));
  if (bytesWritten != static_cast<size_t>(written))
  {
    _sdCardReady = false;
    Serial.println("SD append failed: short write");
    return false;
  }

  // Save last written line for dedupe checks
  strncpy(_lastLine, line, sizeof(_lastLine) - 1);
  _lastLine[sizeof(_lastLine) - 1] = '\0';
  _hasLastLine = true;
  _lastSampleUs = static_cast<uint32_t>(sample.sampleUs);
  _hasLastSampleUs = true;

  // Flush to SD card once per second (every kSamplesPerFlush samples at 80 Hz).
  // This keeps per-sample overhead minimal while bounding data loss to ~1 s.
  if (++_samplesSinceLastFlush >= kSamplesPerFlush)
  {
    _activeFile.flush();
    _samplesSinceLastFlush = 0;
  }

  return true;
}

int RawSdLogger::listFiles(String fileList[], int maxFiles) const
{
  if (!_sdCardReady || maxFiles <= 0)
  {
    return 0;
  }

  int fileCount = 0;
  File inboxDir = SD.open(kInboxRootFolder);
  if (!inboxDir)
  {
    return 0;
  }

  File dayDir = inboxDir.openNextFile();
  while (dayDir && fileCount < maxFiles)
  {
    if (dayDir.isDirectory())
    {
      File csvFile = dayDir.openNextFile();
      while (csvFile && fileCount < maxFiles)
      {
        if (!csvFile.isDirectory())
        {
          char fullPath[96] = {0};
          snprintf(fullPath, sizeof(fullPath), "%s/%s/%s", kInboxRootFolder, dayDir.name(), csvFile.name());
          fileList[fileCount++] = String(fullPath);
        }
        csvFile.close();
        csvFile = dayDir.openNextFile();
      }
    }
    dayDir.close();
    dayDir = inboxDir.openNextFile();
  }
  inboxDir.close();

  // Sort in descending order (most recent first) by comparing filenames
  for (int i = 0; i < fileCount - 1; i++)
  {
    for (int j = i + 1; j < fileCount; j++)
    {
      if (fileList[j] > fileList[i])
      {
        String temp = fileList[i];
        fileList[i] = fileList[j];
        fileList[j] = temp;
      }
    }
  }

  return fileCount;
}

size_t RawSdLogger::getFileSize(const char *filename) const
{
  if (!_sdCardReady || !filename)
  {
    return 0;
  }

  File file = SD.open(filename, FILE_READ);
  if (!file)
  {
    return 0;
  }

  size_t size = file.size();
  file.close();
  return size;
}

bool RawSdLogger::readFile(const char *filename, uint8_t *buffer, size_t bufferSize, size_t &bytesRead)
{
  bytesRead = 0;
  if (!_sdCardReady || !filename || !buffer || bufferSize == 0)
  {
    return false;
  }

  File file = SD.open(filename, FILE_READ);
  if (!file)
  {
    return false;
  }

  bytesRead = file.read(buffer, bufferSize);
  file.close();
  return true;
}
