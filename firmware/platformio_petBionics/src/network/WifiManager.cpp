#include "WifiManager.h"

#include <Preferences.h>
#include <WiFi.h>

namespace
{
  const char *kPrefsNamespace = "petbionic_net";
  const char *kSsidKey = "ssid";
  const char *kPassKey = "pass";
}

WifiManager::WifiManager()
    : _ssid(""),
      _password(""),
      _reconnectPeriodMs(10000),
      _lastReconnectMs(0)
{
}

void WifiManager::begin()
{
  Serial.println("[WiFi] begin");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);

  if (!loadCredentials())
  {
    Serial.println("[WiFi] no saved credentials");
    return;
  }

  Serial.printf("[WiFi] loaded ssid='%s' pass_len=%u\n", _ssid.c_str(), static_cast<unsigned>(_password.length()));
  connect(12000);
}

void WifiManager::loop(uint32_t nowMs)
{
  if (isConnected())
  {
    return;
  }

  if (_ssid.length() == 0)
  {
    return;
  }

  if ((nowMs - _lastReconnectMs) < _reconnectPeriodMs)
  {
    return;
  }

  _lastReconnectMs = nowMs;
  Serial.println("[WiFi] reconnect loop triggered");
  connect(5000);
}

bool WifiManager::setCredentials(const String &ssid, const String &password, bool persist)
{
  if (ssid.length() == 0)
  {
    Serial.println("[WiFi] setCredentials rejected: empty ssid");
    return false;
  }

  _ssid = ssid;
  _password = password;
  Serial.printf("[WiFi] credentials updated ssid='%s' persist=%s\n",
                _ssid.c_str(),
                persist ? "true" : "false");

  if (!persist)
  {
    return true;
  }

  return saveCredentials();
}

bool WifiManager::connect(uint32_t timeoutMs)
{
  if (_ssid.length() == 0)
  {
    Serial.println("[WiFi] connect skipped: no ssid configured");
    return false;
  }

  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == _ssid)
  {
    Serial.println("[WiFi] already connected");
    return true;
  }

  Serial.printf("[WiFi] connecting to '%s' timeout=%lu ms\n", _ssid.c_str(), static_cast<unsigned long>(timeoutMs));
  WiFi.disconnect();
  WiFi.begin(_ssid.c_str(), _password.c_str());

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < timeoutMs)
  {
    delay(250);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[WiFi] connect timeout");
    return false;
  }

  Serial.printf("[WiFi] connected ip=%s rssi=%d\n",
                WiFi.localIP().toString().c_str(),
                WiFi.RSSI());
  return true;
}

bool WifiManager::isConnected() const
{
  return WiFi.status() == WL_CONNECTED;
}

String WifiManager::localIpString() const
{
  if (!isConnected())
  {
    return String("0.0.0.0");
  }

  return WiFi.localIP().toString();
}

String WifiManager::configuredSsid() const
{
  return _ssid;
}

bool WifiManager::loadCredentials()
{
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true))
  {
    Serial.println("[WiFi] preferences open failed for read");
    return false;
  }

  _ssid = prefs.getString(kSsidKey, "");
  _password = prefs.getString(kPassKey, "");
  prefs.end();

  Serial.printf("[WiFi] preferences read ssid='%s' pass_len=%u\n",
                _ssid.c_str(),
                static_cast<unsigned>(_password.length()));
  return _ssid.length() > 0;
}

bool WifiManager::saveCredentials() const
{
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false))
  {
    Serial.println("[WiFi] preferences open failed for write");
    return false;
  }

  prefs.putString(kSsidKey, _ssid);
  prefs.putString(kPassKey, _password);
  prefs.end();

  Serial.printf("[WiFi] credentials saved ssid='%s' pass_len=%u\n",
                _ssid.c_str(),
                static_cast<unsigned>(_password.length()));

  return true;
}
