#pragma once

class LightFilter
{
public:
  explicit LightFilter(float alpha = 0.2f);
  void setAlpha(float alpha);
  float update(float input);
  float value() const { return _currentValue; }
  bool initialized() const { return _hasReceivedSample; }

private:
  float _blendFactor;
  float _currentValue;
  bool _hasReceivedSample;
};
