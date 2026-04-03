#include "RawSensor.h"

#include <HX711.h>

#include "../core/Pinout.h"

namespace
{
  HX711 g_scale;
} // namespace

RawSensor::RawSensor(uint8_t analogPin)
    : _analogPin(analogPin),
      _spi(FSPI),
      _imuReady(false),
      _hxReady(false),
      _lastImuHealthCheckMs(0),
  _lastHxHealthCheckMs(0),
  _hxConsecutiveMisses(0),
  _hxSuspiciousReads(0) {}

void RawSensor::imuWriteRegister(uint8_t reg, uint8_t data)
{
  _spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(PetBionicsPinout::kImuCs, LOW);
  _spi.transfer(reg);
  _spi.transfer(data);
  digitalWrite(PetBionicsPinout::kImuCs, HIGH);
  _spi.endTransaction();
}

void RawSensor::imuReadBytes(uint8_t reg, uint8_t count, uint8_t *dest)
{
  _spi.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE3));
  digitalWrite(PetBionicsPinout::kImuCs, LOW);
  _spi.transfer(reg | 0x80);
  for (uint8_t i = 0; i < count; ++i)
  {
    dest[i] = _spi.transfer(0x00);
  }
  digitalWrite(PetBionicsPinout::kImuCs, HIGH);
  _spi.endTransaction();
}

void RawSensor::begin()
{
  pinMode(_analogPin, INPUT);

  pinMode(PetBionicsPinout::kImuCs, OUTPUT);
  digitalWrite(PetBionicsPinout::kImuCs, HIGH);
  _spi.begin(PetBionicsPinout::kSpiSck,
             PetBionicsPinout::kSpiMiso,
             PetBionicsPinout::kSpiMosi,
             PetBionicsPinout::kImuCs);

  imuWriteRegister(kPwrMgmt1Reg, 0x00);
  delay(20);

  uint8_t whoAmI = 0;
  imuReadBytes(kWhoAmIReg, 1, &whoAmI);
  _imuReady = (whoAmI != 0x00 && whoAmI != 0xFF);

  g_scale.begin(PetBionicsPinout::kHx711Dout, PetBionicsPinout::kHx711Sck);
  _hxReady = g_scale.wait_ready_timeout(1000);
}

void RawSensor::updateHealth(uint32_t nowMs)
{
  const uint32_t kHealthCheckPeriodMs = 500;

  if ((nowMs - _lastImuHealthCheckMs) >= kHealthCheckPeriodMs)
  {
    _lastImuHealthCheckMs = nowMs;
    uint8_t whoAmI = 0;
    imuReadBytes(kWhoAmIReg, 1, &whoAmI);
    _imuReady = (whoAmI != 0x00 && whoAmI != 0xFF);
  }

  if ((nowMs - _lastHxHealthCheckMs) >= kHealthCheckPeriodMs)
  {
    _lastHxHealthCheckMs = nowMs;

    if (g_scale.wait_ready_timeout(2))
    {
      const long raw = g_scale.read();
      const bool suspicious = (raw == 0L || raw == -1L || raw == 8388607L || raw == -8388608L);

      _hxConsecutiveMisses = 0;
      if (suspicious)
      {
        if (_hxSuspiciousReads < 255)
        {
          _hxSuspiciousReads++;
        }
      }
      else
      {
        _hxSuspiciousReads = 0;
      }

      _hxReady = (_hxSuspiciousReads < 3);
    }
    else
    {
      _hxSuspiciousReads = 0;
      if (_hxConsecutiveMisses < 255)
      {
        _hxConsecutiveMisses++;
      }
      if (_hxConsecutiveMisses >= 3)
      {
        _hxReady = false;
      }
    }
  }
}

bool RawSensor::readImuAxes(int16_t &ax, int16_t &ay, int16_t &az,
                            int16_t &gx, int16_t &gy, int16_t &gz)
{
  updateHealth(millis());

  if (!_imuReady)
  {
    ax = ay = az = gx = gy = gz = 0;
    return false;
  }

  uint8_t raw[14];
  imuReadBytes(kAccelXoutHReg, 14, raw);
  ax = static_cast<int16_t>((raw[0] << 8) | raw[1]);
  ay = static_cast<int16_t>((raw[2] << 8) | raw[3]);
  az = static_cast<int16_t>((raw[4] << 8) | raw[5]);
  gx = static_cast<int16_t>((raw[8] << 8) | raw[9]);
  gy = static_cast<int16_t>((raw[10] << 8) | raw[11]);
  gz = static_cast<int16_t>((raw[12] << 8) | raw[13]);
  return true;
}

int32_t RawSensor::readRaw()
{
  updateHealth(millis());

  if (_hxReady && g_scale.wait_ready_timeout(2))
  {
    return static_cast<int32_t>(g_scale.read());
  }

  return static_cast<int32_t>(analogRead(_analogPin));
}

void RawSensor::fillSample(RawSample &sample, uint32_t localMs, uint64_t epochMs, float filtered)
{
  sample.tLocalMs = localMs;
  sample.tEpochMs = epochMs;
  sample.raw = readRaw();
  sample.filtered = filtered;
  readImuAxes(sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz);
}
