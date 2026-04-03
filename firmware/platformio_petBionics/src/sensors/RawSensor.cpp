#include "RawSensor.h"

#include <HX711.h>

#include "../core/Pinout.h"

namespace
{
  HX711 g_scale;
} // namespace

RawSensor::RawSensor(uint8_t analogPin)
    : _analogPin(analogPin), _spi(FSPI), _imuReady(false), _hxReady(false) {}

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

bool RawSensor::readImuAxes(int16_t &ax, int16_t &ay, int16_t &az,
                            int16_t &gx, int16_t &gy, int16_t &gz)
{
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
  if (_hxReady && g_scale.is_ready())
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
