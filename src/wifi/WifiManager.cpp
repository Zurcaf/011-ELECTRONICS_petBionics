#include "WifiManager.h"

bool WifiManager::connect(const char *ssid, const char *password, uint32_t timeoutMs)
{
    if (!ssid || ssid[0] == '\0')
    {
        Serial.println("[WiFi] No SSID configured — skipping");
        return false;
    }

    Serial.printf("[WiFi] Connecting to %s", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    const uint32_t connectionStartMs = millis();
    // Poll until the timeout expires or the station gets an IP address.
    while (WiFi.status() != WL_CONNECTED && (millis() - connectionStartMs) < timeoutMs)
    {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[WiFi] Connection failed");
        return false;
    }

    Serial.printf("[WiFi] Connected — http://%s\n", WiFi.localIP().toString().c_str());
    return true;
}

IPAddress WifiManager::localIP() const
{
    return WiFi.localIP();
}
