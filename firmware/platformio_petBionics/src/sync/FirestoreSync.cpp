#include "FirestoreSync.h"

#include <SD.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ---------------------------------------------------------------------------
// CSV parsing helpers
// ---------------------------------------------------------------------------
static String csvField(const String &line, const int *cp, int col, int totalCols)
{
    int start = (col == 0) ? 0 : cp[col - 1] + 1;
    int end   = (col < totalCols - 1) ? cp[col] : (int)line.length();
    return line.substring(start, end);
}

// ---------------------------------------------------------------------------
// Session metadata document
// ---------------------------------------------------------------------------
int FirestoreSync::uploadSessionDoc(WiFiClientSecure &client,
                                    const String &sessionId,
                                    uint32_t startMs)
{
    String url = String(kBaseUrl) +
                 "/sessions/" + sessionId +
                 "?key=" + kApiKey;

    String body =
        "{\"fields\":{"
        "\"sessionId\":{\"stringValue\":\"" + sessionId + "\"},"
        "\"device\":{\"stringValue\":\"PetBionic_01\"},"
        "\"startMs\":{\"integerValue\":\"" + String(startMs) + "\"},"
        "\"timestamp\":{\"integerValue\":\"" + String(startMs) + "\"}"
        "}}";

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH(body);
    http.end();
    client.stop();
    delay(200);
    return code;
}

// ---------------------------------------------------------------------------
// Upload one reading row via PATCH (works with API key).
// ---------------------------------------------------------------------------
int FirestoreSync::uploadReading(WiFiClientSecure &client,
                                 const String &sessionId,
                                 int index,
                                 const String &csvLine)
{
    const int kCols   = 17;
    const int kCommas = kCols - 1;
    int cp[kCommas];
    int found = 0;

    for (int i = 0; i < (int)csvLine.length() && found < kCommas; ++i)
    {
        if (csvLine[i] == ',') cp[found++] = i;
    }
    if (found < kCommas)
    {
        Serial.printf("[Firestore] Row %d skipped: only %d commas\n", index, found);
        return -1;
    }

    long   t_rel_ms   = csvField(csvLine, cp,  0, kCols).toInt();
    float  load_raw   = csvField(csvLine, cp,  3, kCols).toFloat();
    float  load_filt  = csvField(csvLine, cp,  4, kCols).toFloat();
    float  ax         = csvField(csvLine, cp,  5, kCols).toFloat();
    float  ay         = csvField(csvLine, cp,  6, kCols).toFloat();
    float  az         = csvField(csvLine, cp,  7, kCols).toFloat();
    float  gx         = csvField(csvLine, cp,  8, kCols).toFloat();
    float  gy         = csvField(csvLine, cp,  9, kCols).toFloat();
    float  gz         = csvField(csvLine, cp, 10, kCols).toFloat();
    float  roll       = csvField(csvLine, cp, 14, kCols).toFloat();
    float  pitch      = csvField(csvLine, cp, 15, kCols).toFloat();
    float  yaw        = csvField(csvLine, cp, 16, kCols).toFloat();

    String url = String(kBaseUrl) +
                 "/sessions/" + sessionId +
                 "/readings/" + String(index) +
                 "?key=" + kApiKey;

    String body =
        "{\"fields\":{"
        "\"t_rel_ms\":{\"integerValue\":\"" + String(t_rel_ms) + "\"},"
        "\"load_cell_raw\":{\"doubleValue\":"  + String(load_raw,  3) + "},"
        "\"load_cell_filt\":{\"doubleValue\":" + String(load_filt, 3) + "},"
        "\"imu_ax\":{\"doubleValue\":"  + String(ax,  2) + "},"
        "\"imu_ay\":{\"doubleValue\":"  + String(ay,  2) + "},"
        "\"imu_az\":{\"doubleValue\":"  + String(az,  2) + "},"
        "\"imu_gx\":{\"doubleValue\":"  + String(gx,  2) + "},"
        "\"imu_gy\":{\"doubleValue\":"  + String(gy,  2) + "},"
        "\"imu_gz\":{\"doubleValue\":"  + String(gz,  2) + "},"
        "\"roll_deg\":{\"doubleValue\":"  + String(roll,  2) + "},"
        "\"pitch_deg\":{\"doubleValue\":" + String(pitch, 2) + "},"
        "\"yaw_deg\":{\"doubleValue\":"   + String(yaw,   2) + "}"
        "}}";

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH(body);
    http.end();
    client.stop();
    delay(100);
    return code;
}

// ---------------------------------------------------------------------------
// Main entry point — one PATCH per reading, API key auth.
// ---------------------------------------------------------------------------
SyncResult FirestoreSync::syncFile(const char *filePath, const String &sessionId)
{
    SyncResult result{false, 0, 0};

    if (!filePath || filePath[0] == '\0')
    {
        Serial.println("[Firestore] syncFile: empty path");
        return result;
    }

    File f = SD.open(filePath, FILE_READ);
    if (!f)
    {
        Serial.printf("[Firestore] Cannot open %s\n", filePath);
        return result;
    }

    Serial.printf("[Firestore] Syncing %s -> sessions/%s\n",
                  filePath, sessionId.c_str());

    WiFiClientSecure client;
    client.setInsecure();

    int metaCode = uploadSessionDoc(client, sessionId, millis());
    Serial.printf("[Firestore] Session doc: %d\n", metaCode);
    if (metaCode != 200) result.httpErrorCode = metaCode;

    f.readStringUntil('\n'); // skip header

    int index = 0;
    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        int code = uploadReading(client, sessionId, index, line);
        if (code == 200)
        {
            result.readingsSynced++;
            if (index % 50 == 0)
                Serial.printf("[Firestore] %d readings uploaded...\n", index + 1);
        }
        else
        {
            result.httpErrorCode = code;
            Serial.printf("[Firestore] Reading %d failed: %d\n", index, code);
        }
        index++;
    }

    f.close();

    Serial.printf("[Firestore] Sync complete — %d/%d readings\n",
                  result.readingsSynced, index);

    result.success = (result.readingsSynced == index && index > 0);
    return result;
}
