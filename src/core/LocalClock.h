#pragma once

#include <Arduino.h>

class LocalClock
{
public:
  uint32_t nowMs() const { return millis(); }
};
