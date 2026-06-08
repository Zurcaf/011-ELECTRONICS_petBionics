#pragma once

#include <Arduino.h>
#include "Pinout.h"

// Compile-time WiFi credentials — defined in secrets.ini (gitignored).
#ifndef WIFI_SSID
#  define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#  define WIFI_PASSWORD ""
#endif

struct AppConfig
{
    uint32_t    samplePeriodUs  = 12500;   // 80 Hz
    float       filterAlpha     = 0.2f;
    float       eventThreshold  = 100.0f;
    uint32_t    eventCooldownMs = 300;
    uint8_t     analogPin       = A0;
    uint8_t     sdCsPin         = PetBionicsPinout::kSdCs;
    const char *sdPath          = "/raw_log.csv";

    bool        acquisitionEnabled = false;

    char        wifiSsid    [64] = WIFI_SSID;
    char        wifiPassword[64] = WIFI_PASSWORD;

    // Battery voltage calibration: V_battery = ADC_raw * 3.3 / 4095 * batteryCalibration
    // Empirical: ESP32 has pull-up on A0, ADC reads 2.227V when actual is 1.86V (3.86V battery)
    // Factor = 3.86 / 2.227 = 1.73
    float       batteryCalibration = 1.73f;
    uint8_t     batteryAdcPin = A0;
};

