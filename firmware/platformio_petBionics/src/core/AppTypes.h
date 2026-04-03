#pragma once

#include <Arduino.h>

struct RawSample
{
  uint32_t tLocalMs;
  uint64_t tEpochMs;
  int32_t raw;
  float filtered;
  int16_t ax;
  int16_t ay;
  int16_t az;
  int16_t gx;
  int16_t gy;
  int16_t gz;
};

struct EventInfo
{
  bool triggered;
  float score;
};

struct AppStatus
{
  bool acquisitionEnabled;
  bool sdReady;
  bool imuReady;
  bool hx711Ready;
  uint32_t samples;
  uint32_t events;
};
