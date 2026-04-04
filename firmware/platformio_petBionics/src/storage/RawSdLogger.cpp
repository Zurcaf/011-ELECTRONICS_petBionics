#include "RawSdLogger.h"

#include <SD.h>
#include <SPI.h>
#include <time.h>

#include "../core/Pinout.h"

namespace
{
  void configureLocalTimezone()
  {
    setenv("TZ", "WET0WEST,M3.5.0/1,M10.5.0/2", 1);
    tzset();
  }

  struct SessionFileRecord
  {
    char currentPath[96];
    uint16_t runNumber;
  };

  void extractBaseName(const char *path, char *out, size_t outSize);

  void normalizeBaseName(char *base)
  {
    if (!base)
    {
      return;
    }
    if (base[0] == '/')
    {
      memmove(base, base + 1, strlen(base));
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

  bool buildDateKey(uint64_t epochMs, char *out, size_t outSize)
  {
    if (!out || outSize < 9 || epochMs == 0)
    {
      return false;
    }

    const time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
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

  void buildDayFolderPath(uint64_t epochMs, char *out, size_t outSize)
  {
    if (!out || outSize == 0)
    {
      return;
    }

    char dateKey[9] = {0};
    if (!buildDateKey(epochMs, dateKey, sizeof(dateKey)))
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

  uint16_t nextDailyRunNumber(const char *basePath, const char *dayFolder, uint64_t epochMs)
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

  bool canAccessPath(bool sessionOpen, const char *activeFilePath)
  {
    File probe = sessionOpen
                     ? SD.open(activeFilePath, FILE_APPEND)
                     : SD.open("/", FILE_READ);
    if (!probe)
    {
      return false;
    }
    probe.close();
    return true;
  }

  void extractBaseName(const char *path, char *out, size_t outSize)
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

  void buildSessionPath(const char *basePath, const char *dayFolder, uint64_t epochMs, uint16_t runNumber, char *out, size_t outSize)
  {
    if (!basePath || !dayFolder || !out || outSize == 0)
    {
      return;
    }

    char base[64] = {0};
    extractBaseName(basePath, base, sizeof(base));
    if (base[0] == '\0')
    {
      strncpy(base, "/raw_log", sizeof(base) - 1);
    }
    normalizeBaseName(base);

    if (epochMs > 0)
    {
      time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
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

  void formatDateTimeUtc(uint64_t epochMs, char *out, size_t outSize)
  {
    if (!out || outSize == 0)
    {
      return;
    }

    if (epochMs == 0)
    {
      snprintf(out, outSize, "UNSYNCED");
      return;
    }

    const time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
    const uint32_t millisPart = static_cast<uint32_t>(epochMs % 1000ULL);
    struct tm localTm;
    localtime_r(&seconds, &localTm);

    snprintf(out,
             outSize,
             "%04d-%02d-%02dT%02d:%02d:%02d.%03luZ",
             localTm.tm_year + 1900,
             localTm.tm_mon + 1,
             localTm.tm_mday,
             localTm.tm_hour,
             localTm.tm_min,
             localTm.tm_sec,
             static_cast<unsigned long>(millisPart));
  }

  void formatTimeUtc(uint64_t epochMs, char *out, size_t outSize)
  {
    if (!out || outSize == 0)
    {
      return;
    }

    if (epochMs == 0)
    {
      snprintf(out, outSize, "UNSYNCED");
      return;
    }

    const time_t seconds = static_cast<time_t>(epochMs / 1000ULL);
    const uint32_t millisPart = static_cast<uint32_t>(epochMs % 1000ULL);
    struct tm localTm;
    localtime_r(&seconds, &localTm);

    snprintf(out,
             outSize,
             "%02d:%02d:%02d.%03lu",
             localTm.tm_hour,
             localTm.tm_min,
             localTm.tm_sec,
             static_cast<unsigned long>(millisPart));
  }

} // namespace

RawSdLogger::RawSdLogger(uint8_t csPin, const char *filePath)
    : _csPin(csPin),
      _filePath(filePath),
      _ready(false),
      _spi(FSPI),
      _lastHealthCheckMs(0),
      _sessionOpen(false),
      _sessionStartEpochMs(0),
      _flushCounter(0)
{
  _activeFilePath[0] = '\0';
}

bool RawSdLogger::begin()
{
  configureLocalTimezone();
  _spi.begin(PetBionicsPinout::kSpiSck,
             PetBionicsPinout::kSpiMiso,
             PetBionicsPinout::kSpiMosi);

  _ready = SD.begin(_csPin, _spi);
  if (!_ready)
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
  if (!_ready)
  {
    Serial.println("SD session start failed: logger not ready");
    return false;
  }

  char dayFolder[32] = {0};
  buildDayFolderPath(startEpochMs, dayFolder, sizeof(dayFolder));

  if (dayFolder[0] == '\0')
  {
    Serial.println("SD session start failed: invalid day folder");
    return false;
  }

  // Ensure /inbox exists before creating the day subfolder (SD.mkdir is not recursive).
  if (!SD.exists(kInboxFolder) && !SD.mkdir(kInboxFolder))
  {
    Serial.println("SD session start failed: could not create inbox folder");
    return false;
  }
  if (!SD.exists(dayFolder) && !SD.mkdir(dayFolder))
  {
    Serial.println("SD session start failed: could not create day folder");
    return false;
  }

  uint16_t runNumber = nextDailyRunNumber(_filePath, dayFolder, startEpochMs);
  if (runNumber == 0)
  {
    runNumber = 1;
  }

  buildSessionPath(_filePath, dayFolder, startEpochMs, runNumber, _activeFilePath, sizeof(_activeFilePath));
  _sessionOpen = true;
  _sessionStartEpochMs = startEpochMs;

  if (!ensureHeader(_activeFilePath, _sessionStartEpochMs))
  {
    _sessionOpen = false;
    _sessionStartEpochMs = 0;
    _activeFilePath[0] = '\0';
    return false;
  }

  // Open once and keep the handle open for the whole session so that each
  // append() only writes – no per-sample open/close overhead on the SD.
  _activeFile = SD.open(_activeFilePath, FILE_APPEND);
  if (!_activeFile)
  {
    _ready = false;
    _sessionOpen = false;
    _sessionStartEpochMs = 0;
    _activeFilePath[0] = '\0';
    Serial.println("SD session start failed: could not open file for append");
    return false;
  }
  _flushCounter = 0;

  Serial.printf("SD session file: %s\n", _activeFilePath);
  return true;
}

void RawSdLogger::stopSession()
{
  if (_activeFile)
  {
    _activeFile.flush();
    _activeFile.close();
  }
  _sessionOpen = false;
  _sessionStartEpochMs = 0;
  _activeFilePath[0] = '\0';
  _flushCounter = 0;
}

void RawSdLogger::updateHealth(uint32_t nowMs)
{
  const uint32_t kHealthCheckPeriodMs = 1000;
  if ((nowMs - _lastHealthCheckMs) < kHealthCheckPeriodMs)
  {
    return;
  }
  _lastHealthCheckMs = nowMs;

  if (_ready)
  {
    if (!isCardPresent() || !canAccessPath(_sessionOpen, _activeFilePath))
    {
      _ready = false;
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
      _ready = false;
      return;
    }

    _ready = canAccessPath(_sessionOpen, _activeFilePath);
    if (_ready && _sessionOpen)
    {
      _ready = ensureHeader(_activeFilePath, _sessionStartEpochMs);
    }
    if (_ready)
    {
      Serial.println("SD logger recovered");
    }
  }
}

bool RawSdLogger::ensureHeader(const char *path, uint64_t startEpochMs)
{
  if (!path || path[0] == '\0')
  {
    _ready = false;
    Serial.println("SD header create failed: empty file path");
    return false;
  }

  if (SD.exists(path))
  {
    return true;
  }

  File file = SD.open(path, FILE_WRITE);
  if (!file)
  {
    _ready = false;
    Serial.println("SD header create failed: could not open log file");
    return false;
  }

  file.println("t_rel_ms,t_rel_us,time_local,load_cell_raw,load_cell_filt,imu_ax,imu_ay,imu_az,imu_gx,imu_gy,imu_gz,imu_mx,imu_my,imu_mz,roll_deg,pitch_deg,yaw_deg");
  file.close();
  return true;
}

bool RawSdLogger::append(const RawSample &sample, const EventInfo &event)
{
  (void)event;

  if (!_ready || !_sessionOpen || !_activeFile)
  {
    return false;
  }

  char timeUtc[24];
  formatTimeUtc(sample.tEpochMs, timeUtc, sizeof(timeUtc));

  char line[320];
  int written = snprintf(line, sizeof(line),
                         "%lu,%lu,%s,%ld,%.3f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f\n",
                         static_cast<unsigned long>(sample.tLocalMs),
                         static_cast<unsigned long>(sample.tLocalUs),
                         timeUtc,
                         static_cast<long>(sample.raw),
                         sample.filtered,
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

  size_t bytesWritten = _activeFile.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(written));
  if (bytesWritten != static_cast<size_t>(written))
  {
    _ready = false;
    Serial.println("SD append failed: short write");
    return false;
  }

  // Flush to SD card once per second (every kFlushEveryN samples at 80 Hz).
  // This keeps per-sample overhead minimal while bounding data loss to ~1 s.
  if (++_flushCounter >= kFlushEveryN)
  {
    _activeFile.flush();
    _flushCounter = 0;
  }

  return true;
}

bool RawSdLogger::markAsSent(const char *filePath)
{
  if (!filePath || filePath[0] == '\0')
  {
    return false;
  }

  // Build destination by replacing the /inbox prefix with /sent.
  // e.g. /inbox/20260404/run001_20260404_143022.csv
  //   ->  /sent/20260404/run001_20260404_143022.csv
  const size_t inboxLen = strlen(kInboxFolder);
  if (strncmp(filePath, kInboxFolder, inboxLen) != 0)
  {
    Serial.printf("markAsSent: path does not start with %s: %s\n", kInboxFolder, filePath);
    return false;
  }

  char destPath[128];
  snprintf(destPath, sizeof(destPath), "%s%s", kSentFolder, filePath + inboxLen);

  // Ensure /sent exists.
  if (!SD.exists(kSentFolder) && !SD.mkdir(kSentFolder))
  {
    Serial.println("markAsSent: could not create sent folder");
    return false;
  }

  // Ensure the day subfolder under /sent exists.
  const char *lastSlash = strrchr(destPath, '/');
  if (lastSlash && lastSlash != destPath)
  {
    char sentDayFolder[64];
    size_t len = static_cast<size_t>(lastSlash - destPath);
    if (len < sizeof(sentDayFolder))
    {
      strncpy(sentDayFolder, destPath, len);
      sentDayFolder[len] = '\0';
      if (!SD.exists(sentDayFolder) && !SD.mkdir(sentDayFolder))
      {
        Serial.printf("markAsSent: could not create folder %s\n", sentDayFolder);
        return false;
      }
    }
  }

  if (!SD.rename(filePath, destPath))
  {
    Serial.printf("markAsSent: rename failed %s -> %s\n", filePath, destPath);
    return false;
  }

  Serial.printf("markAsSent: %s -> %s\n", filePath, destPath);
  return true;
}
