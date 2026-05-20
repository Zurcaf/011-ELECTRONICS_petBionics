#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WifiManager
{
public:
    bool connect(const char *ssid, const char *password, uint32_t timeoutMs = 10000);
    IPAddress localIP() const;
};
