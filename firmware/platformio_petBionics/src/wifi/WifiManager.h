#pragma once

#include <Arduino.h>

// Maximum time to wait for a WiFi connection before giving up.
static constexpr uint32_t kWifiConnectTimeoutMs = 10000;

class WifiManager
{
public:
    // Attempt to connect using the provided credentials.
    // Returns true if connected within kWifiConnectTimeoutMs.
    bool connect(const char *ssid, const char *password);

    // Gracefully disconnect and power down the radio.
    void disconnect();

    // Returns true if currently associated with an AP.
    bool isConnected() const;
};