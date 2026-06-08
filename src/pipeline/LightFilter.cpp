#include "LightFilter.h"

#include <Arduino.h>

LightFilter::LightFilter(float alpha) : _blendFactor(alpha), _currentValue(0.0f), _hasReceivedSample(false) {}

void LightFilter::setAlpha(float alpha)
{
  _blendFactor = constrain(alpha, 0.0f, 1.0f);
}

float LightFilter::update(float input)
{
  if (!_hasReceivedSample)
  {
    _currentValue = input;
    _hasReceivedSample = true;
    return _currentValue;
  }

  _currentValue = _blendFactor * input + (1.0f - _blendFactor) * _currentValue;
  return _currentValue;
}
