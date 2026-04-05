#pragma once

#include <Arduino.h>
#include "Pinout.h"

// Maximum length for WiFi credentials stored in config.
static constexpr size_t kWifiSsidMaxLen     = 64;
static constexpr size_t kWifiPasswordMaxLen = 64;

struct AppConfig
{
    uint32_t samplePeriodUs   = 12500;    // 80 Hz
    float    filterAlpha      = 0.2f;
    float    eventThreshold   = 100.0f;
    uint32_t eventCooldownMs  = 300;
    uint8_t  analogPin        = A0;
    bool     acquisitionEnabled = false;
    uint8_t  sdCsPin          = PetBionicsPinout::kSdCs;
    const char *sdPath        = "/raw_log.csv";

    // ── WiFi credentials (set via BLE WIFI=ssid:password command) ──────────
    bool wifiEnabled          = false;
    char wifiSsid    [kWifiSsidMaxLen]     = {};
    char wifiPassword[kWifiPasswordMaxLen] = {};
};