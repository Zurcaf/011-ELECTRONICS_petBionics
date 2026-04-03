#include "RawSdLogger.h"

#include <SD.h>
#include <SPI.h>

#include "../core/Pinout.h"

RawSdLogger::RawSdLogger(uint8_t csPin, const char *filePath)
    : _csPin(csPin),
      _filePath(filePath),
      _ready(false),
      _spi(FSPI),
      _lastHealthCheckMs(0) {}

bool RawSdLogger::begin()
{
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

  return ensureHeader();
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
    File probe = SD.open(_filePath, FILE_APPEND);
    if (!probe)
    {
      _ready = false;
      Serial.println("SD health check failed: card/file unavailable");
      return;
    }
    probe.close();
    return;
  }

  // Attempt recovery if the card was reinserted.
  if (SD.begin(_csPin, _spi))
  {
    _ready = ensureHeader();
    if (_ready)
    {
      Serial.println("SD logger recovered");
    }
  }
}

bool RawSdLogger::ensureHeader()
{
  if (SD.exists(_filePath))
  {
    return true;
  }

  File file = SD.open(_filePath, FILE_WRITE);
  if (!file)
  {
    _ready = false;
    Serial.println("SD header create failed: could not open log file");
    return false;
  }

  file.println("t_rel_ms,t_real_ms,load_cell_raw,load_cell_filt,imu_ax,imu_ay,imu_az,imu_gx,imu_gy,imu_gz,event,score");
  file.close();
  return true;
}

bool RawSdLogger::append(const RawSample &sample, const EventInfo &event)
{
  updateHealth(millis());

  if (!_ready)
  {
    return false;
  }

  File file = SD.open(_filePath, FILE_APPEND);
  if (!file)
  {
    _ready = false;
    Serial.println("SD append failed: could not open log file");
    return false;
  }

  char line[192];
  int written = snprintf(line, sizeof(line),
                         "%lu,%llu,%ld,%.3f,%d,%d,%d,%d,%d,%d,%u,%.3f\n",
                         static_cast<unsigned long>(sample.tLocalMs),
                         static_cast<unsigned long long>(sample.tEpochMs),
                         static_cast<long>(sample.raw),
                         sample.filtered,
                         static_cast<int>(sample.ax),
                         static_cast<int>(sample.ay),
                         static_cast<int>(sample.az),
                         static_cast<int>(sample.gx),
                         static_cast<int>(sample.gy),
                         static_cast<int>(sample.gz),
                         event.triggered ? 1U : 0U,
                         event.score);
  if (written <= 0 || written >= static_cast<int>(sizeof(line)))
  {
    file.close();
    Serial.println("SD append failed: line formatting error");
    return false;
  }

  size_t bytesWritten = file.write(reinterpret_cast<const uint8_t *>(line), static_cast<size_t>(written));
  if (bytesWritten != static_cast<size_t>(written))
  {
    _ready = false;
    file.close();
    Serial.println("SD append failed: short write");
    return false;
  }

  file.close();
  return true;
}
