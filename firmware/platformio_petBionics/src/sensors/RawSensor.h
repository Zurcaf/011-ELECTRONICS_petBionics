#pragma once

#include <Arduino.h>
#include <SPI.h>

#include "../core/AppTypes.h"

class RawSensor
{
public:
  explicit RawSensor(uint8_t analogPin);
  void begin();
  void updateHealth(uint32_t nowMs);
  int32_t readRaw();
  bool readImuAxes(int16_t &ax, int16_t &ay, int16_t &az,
                   int16_t &gx, int16_t &gy, int16_t &gz);
  void fillSample(RawSample &sample, uint32_t localMs, uint64_t epochMs, float filtered);
  bool isImuReady() const { return _imuReady; }
  bool isHx711Ready() const { return _hxReady; }

private:
  static constexpr uint8_t kWhoAmIReg = 0x75;
  static constexpr uint8_t kPwrMgmt1Reg = 0x6B;
  static constexpr uint8_t kAccelXoutHReg = 0x3B;

  uint8_t _analogPin;
  SPIClass _spi;
  bool _imuReady;
  bool _hxReady;
  uint32_t _lastImuHealthCheckMs;
  uint32_t _lastHxHealthCheckMs;
  uint8_t _hxConsecutiveMisses;
  uint8_t _hxSuspiciousReads;

  void imuWriteRegister(uint8_t reg, uint8_t data);
  void imuReadBytes(uint8_t reg, uint8_t count, uint8_t *dest);
};
