#pragma once

#include <Arduino.h>
#include "Pinout.h"

struct AppConfig
{
  uint32_t samplePeriodUs = 12500; // 80 Hz
  float filterAlpha = 0.2f;      // light EMA filter
  float eventThreshold = 100.0f; // abs(filtered - raw)
  uint32_t eventCooldownMs = 300;
  uint8_t analogPin = A0; // fallback raw source
  bool acquisitionEnabled = false;

  uint8_t sdCsPin = PetBionicsPinout::kSdCs;
  const char *sdPath = "/raw_log.csv";
};
