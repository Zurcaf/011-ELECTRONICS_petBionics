#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

// Replace with your network credentials
const char* ssid     = "composites";
const char* password = "Composites2019";

// NTP Server settings
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;      // Adjust for your timezone in seconds
const int   daylightOffset_sec = 3600; // Adjust if DST applies

// Server to send data to
const char* serverUrl = "http://your-server.com/data";

void setup() {
  Serial.begin(115200);
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  // Initialize NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("NTP time sync initialized");
}

String getTimeString() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "Failed to obtain time";
  }
  char buffer[64];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

void loop() {
  if(WiFi.status() == WL_CONNECTED){
    String timestamp = getTimeString();
    String data = "Hello from ESP32 at " + timestamp;

    // Send data via HTTP POST
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int httpResponseCode = http.POST("data=" + data);
    
    if(httpResponseCode>0){
      Serial.println("Data sent successfully: " + String(httpResponseCode));
    } else {
      Serial.println("Error sending data: " + String(httpResponseCode));
    }
    http.end();

    delay(500); // send every 0.5s
  } else {
    Serial.println("WiFi not connected");
    delay(1000);
  }
}
