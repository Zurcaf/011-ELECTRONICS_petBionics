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
                   int16_t &gx, int16_t &gy, int16_t &gz,
                   int16_t &mx, int16_t &my, int16_t &mz);
  void fillSample(RawSample &sample, uint32_t localMs, uint64_t epochMs, float filtered);
  bool isImuReady() const { return _imuReady; }
  bool isHx711Ready() const { return _hxReady; }

private:
  static constexpr uint8_t kWhoAmIReg = 0x75;
  static constexpr uint8_t kPwrMgmt1Reg = 0x6B;
  static constexpr uint8_t kAccelXoutHReg = 0x3B;
  static constexpr uint8_t kUserCtrlReg = 0x6A;
  static constexpr uint8_t kI2cMstCtrlReg = 0x24;
  static constexpr uint8_t kI2cSlv0AddrReg = 0x25;
  static constexpr uint8_t kI2cSlv0RegReg = 0x26;
  static constexpr uint8_t kI2cSlv0CtrlReg = 0x27;
  static constexpr uint8_t kI2cSlv0DoReg = 0x63;
  static constexpr uint8_t kExtSensData00Reg = 0x49;

  static constexpr uint8_t kAk8963Address = 0x0C;
  static constexpr uint8_t kAkWhoAmIReg = 0x00;
  static constexpr uint8_t kAkSt1Reg = 0x02;
  static constexpr uint8_t kAkCntl1Reg = 0x0A;
  static constexpr uint8_t kAkWhoAmIValue = 0x48;

  uint8_t _analogPin;
  SPIClass _spi;
  bool _imuReady;
  bool _hxReady;
  uint32_t _lastImuHealthCheckMs;
  uint32_t _lastHxHealthCheckMs;
  uint8_t _hxConsecutiveMisses;
  uint8_t _hxConsecutiveHits;
  uint8_t _hxSuspiciousReads;

  void imuWriteRegister(uint8_t reg, uint8_t data);
  void imuReadBytes(uint8_t reg, uint8_t count, uint8_t *dest);
  bool akWriteRegister(uint8_t reg, uint8_t data);
  bool akReadBytes(uint8_t reg, uint8_t count, uint8_t *dest);
};
