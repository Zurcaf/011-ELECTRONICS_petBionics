#include "WifiManager.h"
#include <WiFi.h>

bool WifiManager::connect(const char *ssid, const char *password)
{
    if (isConnected())
    {
        return true;
    }

    if (!ssid || ssid[0] == '\0')
    {
        Serial.println("[WiFi] No SSID configured — skipping connect");
        return false;
    }

    Serial.printf("[WiFi] Connecting to '%s'...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, (password && password[0] != '\0') ? password : nullptr);

    const uint32_t deadline = millis() + kWifiConnectTimeoutMs;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline)
    {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }

    Serial.printf("[WiFi] Failed (status %d)\n", static_cast<int>(WiFi.status()));
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
}

void WifiManager::disconnect()
{
    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_OFF);
    delay(200);
    Serial.println("[WiFi] Disconnected");
}

bool WifiManager::isConnected() const
{
    return WiFi.status() == WL_CONNECTED;
}