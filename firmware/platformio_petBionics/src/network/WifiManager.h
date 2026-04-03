#pragma once

#include <Arduino.h>

class WifiManager
{
public:
  WifiManager();

  void begin();
  void loop(uint32_t nowMs);

  bool setCredentials(const String &ssid, const String &password, bool persist);
  bool connect(uint32_t timeoutMs);

  bool isConnected() const;
  String localIpString() const;
  String configuredSsid() const;

private:
  String _ssid;
  String _password;
  uint32_t _reconnectPeriodMs;
  uint32_t _lastReconnectMs;

  bool loadCredentials();
  bool saveCredentials() const;
};
