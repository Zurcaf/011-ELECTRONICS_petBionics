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

    // ── Sync control ─────────────────────────────────────────────────────
    // Set by BleControl when SYNC command is received; cleared by app loop.
    bool syncRequested              = false;
    // Path of the last closed session file; used by SYNC command.
    char lastSessionFilePath[96]    = {};
    // Written by sync FreeRTOS task; read+cleared by app loop to notify BLE.
    // 0=none, 1=ok, 2=fail, 3=no-wifi, 5=no-pending
    volatile int syncResultCode     = 0;
    // Number of files successfully uploaded in the last sync-all pass.
    volatile int syncSentCount      = 0;
    // Set by sync task when done; main loop must restart BLE advertising.
    volatile bool bleRestartNeeded  = false;
};
