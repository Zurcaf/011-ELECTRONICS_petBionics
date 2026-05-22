#include "SimpleEventDetector.h"

#include <math.h>

SimpleEventDetector::SimpleEventDetector(float threshold, uint32_t cooldownMs)
    : _eventThreshold(threshold), _eventCooldownMs(cooldownMs), _lastTriggeredMs(0) {}

EventInfo SimpleEventDetector::update(float rawValue, float filteredValue, uint32_t nowMs)
{
  float eventScore = fabsf(rawValue - filteredValue);
  bool cooldownElapsed = (nowMs - _lastTriggeredMs) >= _eventCooldownMs;
  bool eventTriggered = eventScore >= _eventThreshold && cooldownElapsed;

  if (eventTriggered)
  {
    _lastTriggeredMs = nowMs;
  }

  return EventInfo{eventTriggered, eventScore};
}
