#pragma once

#include <Arduino.h>

#include "../core/AppTypes.h"

class SimpleEventDetector
{
public:
  SimpleEventDetector(float threshold, uint32_t cooldownMs);
  EventInfo update(float rawValue, float filteredValue, uint32_t nowMs);
  void setThreshold(float threshold) { _eventThreshold = threshold; }

private:
  float _eventThreshold;
  uint32_t _eventCooldownMs;
  uint32_t _lastTriggeredMs;
};
