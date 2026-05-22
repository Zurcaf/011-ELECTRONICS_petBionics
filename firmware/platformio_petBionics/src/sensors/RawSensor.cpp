#include "RawSensor.h"

#include <HX711.h>
#include <math.h>

#include "../core/Pinout.h"

namespace
{
  HX711 g_scale;
} // namespace

RawSensor::RawSensor(uint8_t analogPin)
    : _loadCellAnalogPin(analogPin),
      _spi(FSPI),
      _imuReady(false),
      _hx711Ready(false),
      _lastImuHealthCheckMs(0),
      _lastHxHealthCheckMs(0),
      _imuConsecutiveMisses(0),
      _imuConsecutiveHits(0),
      _hxConsecutiveMisses(0),
      _hxConsecutiveHits(0),
      _hxSuspiciousReads(0),
      _magUsesSingleMeasurementMode(false)
{
}

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

bool RawSensor::akWriteRegister(uint8_t reg, uint8_t data)
{
  imuWriteRegister(kI2cSlv0AddrReg, kAk8963Address);
  imuWriteRegister(kI2cSlv0RegReg, reg);
  imuWriteRegister(kI2cSlv0DoReg, data);
  imuWriteRegister(kI2cSlv0CtrlReg, 0x81);
  delay(2);
  imuWriteRegister(kI2cSlv0CtrlReg, 0x00);
  return true;
}

bool RawSensor::akReadBytes(uint8_t reg, uint8_t count, uint8_t *dest)
{
  if (!dest || count == 0 || count > 24)
  {
    return false;
  }

  imuWriteRegister(kI2cSlv0AddrReg, static_cast<uint8_t>(kAk8963Address | 0x80));
  imuWriteRegister(kI2cSlv0RegReg, reg);
  imuWriteRegister(kI2cSlv0CtrlReg, static_cast<uint8_t>(0x80 | count));
  delay(2);
  imuReadBytes(kExtSensData00Reg, count, dest);
  imuWriteRegister(kI2cSlv0CtrlReg, 0x00);
  return true;
}

void RawSensor::begin()
{
  pinMode(_loadCellAnalogPin, INPUT);

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

  if (_imuReady)
  {
    imuWriteRegister(kUserCtrlReg, 0x20);
    delay(10);
    imuWriteRegister(kI2cMstCtrlReg, 0x00); // Fast I2C clock (348 kHz)
    delay(10);

    uint8_t akWhoAmI = 0;
    if (akReadBytes(kAkWhoAmIReg, 1, &akWhoAmI) && akWhoAmI == kAkWhoAmIValue)
    {
      Serial.printf("[Mag] AK8963 detected (WHO_AM_I=0x%02x)\n", akWhoAmI);

      // Try to set continuous mode
      akWriteRegister(kAkCntl1Reg, 0x00); // Power down
      delay(30);
      akWriteRegister(kAkCntl1Reg, 0x16); // Continuous 16-bit 100Hz
      delay(30);

      // Verify the mode was set
      uint8_t cntl1 = 0;
      akReadBytes(kAkCntl1Reg, 1, &cntl1);
      Serial.printf("[Mag] CNTL1 after write: 0x%02x (expected 0x16)\n", cntl1);

      if (cntl1 != 0x16)
      {
        Serial.println("[Mag] Continuous mode write failed → using single measurement mode");
        _magUsesSingleMeasurementMode = true;
        akWriteRegister(kAkCntl1Reg, 0x12); // Single measurement, 16-bit
        delay(10);
      }
    }
    else
    {
      Serial.printf("[Mag] AK8963 detection failed (WHO_AM_I=0x%02x, expected 0x%02x)\n",
                    akWhoAmI, kAkWhoAmIValue);
    }
  }

  g_scale.begin(PetBionicsPinout::kHx711Dout, PetBionicsPinout::kHx711Sck);
  _hx711Ready = g_scale.wait_ready_timeout(1000);
  if (_hx711Ready)
  {
    g_scale.set_scale(kHx711CalibrationFactor);
    g_scale.tare();
  }
}

void RawSensor::updateHealth(uint32_t nowMs)
{
  const uint32_t kHealthCheckPeriodMs = 500;

  if ((nowMs - _lastImuHealthCheckMs) >= kHealthCheckPeriodMs)
  {
    const uint8_t kImuMissesToNotReady = 4;
    const uint8_t kImuHitsToReady = 2;

    _lastImuHealthCheckMs = nowMs;
    uint8_t whoAmI = 0;
    imuReadBytes(kWhoAmIReg, 1, &whoAmI);
    const bool imuResponding = (whoAmI != 0x00 && whoAmI != 0xFF);
    if (imuResponding)
    {
      _imuConsecutiveMisses = 0;
      if (_imuConsecutiveHits < 255)
      {
        _imuConsecutiveHits++;
      }
      if (_imuConsecutiveHits >= kImuHitsToReady)
      {
        _imuReady = true;
      }
    }
    else
    {
      _imuConsecutiveHits = 0;
      if (_imuConsecutiveMisses < 255)
      {
        _imuConsecutiveMisses++;
      }
      if (_imuConsecutiveMisses >= kImuMissesToNotReady)
      {
        _imuReady = false;
      }
    }
  }

  if ((nowMs - _lastHxHealthCheckMs) >= kHealthCheckPeriodMs)
  {
    // Use hysteresis so short read glitches do not flip HX711 state.
    const uint8_t kMissesToNotReady = 8;
    const uint8_t kHitsToReady = 2;
    const uint8_t kSuspiciousToNotReady = 6;

    _lastHxHealthCheckMs = nowMs;

    if (g_scale.wait_ready_timeout(10))
    {
      const long raw = g_scale.read();
      // 0 and -1 indicate missing/stuck output; 8388607 and -8388608 are the
      // maximum positive and negative values of the HX711's 24-bit signed ADC,
      // meaning the input is railed/saturated.
      const bool suspicious = (raw == 0L || raw == -1L || raw == 8388607L || raw == -8388608L);

      _hxConsecutiveMisses = 0;
      if (suspicious)
      {
        _hxConsecutiveHits = 0;
        if (_hxSuspiciousReads < 255)
        {
          _hxSuspiciousReads++;
        }
        if (_hxSuspiciousReads >= kSuspiciousToNotReady)
        {
          _hx711Ready = false;
        }
      }
      else
      {
        _hxSuspiciousReads = 0;
        if (_hxConsecutiveHits < 255)
        {
          _hxConsecutiveHits++;
        }
        if (_hxConsecutiveHits >= kHitsToReady)
        {
          _hx711Ready = true;
        }
      }
    }
    else
    {
      _hxConsecutiveHits = 0;
      _hxSuspiciousReads = 0;
      if (_hxConsecutiveMisses < 255)
      {
        _hxConsecutiveMisses++;
      }
      if (_hxConsecutiveMisses >= kMissesToNotReady)
      {
        _hx711Ready = false;
      }
    }
  }
}

bool RawSensor::readImuAxes(int16_t &ax, int16_t &ay, int16_t &az,
                            int16_t &gx, int16_t &gy, int16_t &gz,
                            int16_t &mx, int16_t &my, int16_t &mz)
{
  updateHealth(millis());

  if (!_imuReady)
  {
    ax = ay = az = gx = gy = gz = mx = my = mz = 0;
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

  uint8_t magRaw[8] = {0};

  // In single measurement mode, trigger a new measurement before reading
  if (_magUsesSingleMeasurementMode)
  {
    akWriteRegister(kAkCntl1Reg, 0x12); // Single measurement, 16-bit
    delay(10);                          // AK8963 needs ~5ms to complete measurement
  }

  if (akReadBytes(kAkSt1Reg, sizeof(magRaw), magRaw))
  {
    const bool dataReady = (magRaw[0] & 0x01) != 0;
    const bool overflow = (magRaw[7] & 0x08) != 0;

    if (!overflow)
    {
      mx = static_cast<int16_t>((magRaw[2] << 8) | magRaw[1]);
      my = static_cast<int16_t>((magRaw[4] << 8) | magRaw[3]);
      mz = static_cast<int16_t>((magRaw[6] << 8) | magRaw[5]);
    }
    else
    {
      mx = my = mz = 0;
    }
  }
  else
  {
    mx = my = mz = 0;
  }

  return true;
}

int32_t RawSensor::readRaw()
{
  updateHealth(millis());

  if (_hx711Ready && g_scale.wait_ready_timeout(2))
  {
    return static_cast<int32_t>(g_scale.read());
  }

  return static_cast<int32_t>(analogRead(_loadCellAnalogPin));
}

float RawSensor::readEstimatedWeightKg()
{
  updateHealth(millis());

  if (!_hx711Ready)
  {
    return NAN;
  }

  // HX711 at 80 Hz has a 12.5 ms sample period, so a 2 ms timeout is too short.
  // Wait long enough for the next conversion instead of dropping to NAN.
  if (!g_scale.wait_ready_timeout(15))
  {
    return NAN;
  }

  return g_scale.get_units(1);
}

void RawSensor::fillSample(RawSample &sample, uint32_t sampleUs, float estimatedWeightKg)
{
  sample.sampleUs = sampleUs;
  sample.loadCellRaw = readRaw();
  sample.loadCellEstimatedKg = estimatedWeightKg;
  readImuAxes(sample.ax, sample.ay, sample.az,
              sample.gx, sample.gy, sample.gz,
              sample.mx, sample.my, sample.mz);
}
