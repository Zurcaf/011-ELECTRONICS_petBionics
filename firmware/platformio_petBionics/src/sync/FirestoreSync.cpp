#include "FirestoreSync.h"

#include <SD.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ---------------------------------------------------------------------------
// Helper: find the n-th comma position in a String (0-indexed).
// Returns -1 if not found.
// ---------------------------------------------------------------------------
static int nthComma(const String &s, int n)
{
    int found = -1;
    int count = 0;
    for (int i = 0; i < (int)s.length(); ++i)
    {
        if (s[i] == ',')
        {
            if (count == n)
            {
                return i;
            }
            ++count;
            found = i;
        }
    }
    (void)found;
    return -1;
}

// Convenience: field between comma[n-1] and comma[n].
// Pass commaPos[] pre-computed from the line.
static String field(const String &line, const int *cp, int col, int totalCols)
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
        "\"startMs\":{\"integerValue\":\"" + String(startMs) + "\"}"
        "}}";

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH(body);
    http.end();
    return code;
}

// ---------------------------------------------------------------------------
// Single reading upload
// CSV column layout (friend's RawSdLogger, 17 columns, 0-indexed):
//   0  t_rel_ms
//   1  t_rel_us
//   2  time_local      <-- NEW vs your old format
//   3  load_cell_raw
//   4  load_cell_filt
//   5  imu_ax
//   6  imu_ay
//   7  imu_az
//   8  imu_gx
//   9  imu_gy
//  10  imu_gz
//  11  imu_mx
//  12  imu_my
//  13  imu_mz
//  14  roll_deg
//  15  pitch_deg
//  16  yaw_deg
// ---------------------------------------------------------------------------
int FirestoreSync::uploadReading(WiFiClientSecure &client,
                                 const String &sessionId,
                                 int index,
                                 const String &csvLine)
{
    // Pre-compute comma positions (we expect exactly 16 commas = 17 fields).
    const int kCols   = 17;
    const int kCommas = kCols - 1;
    int cp[kCommas];
    int found = 0;
    for (int i = 0; i < (int)csvLine.length() && found < kCommas; ++i)
    {
        if (csvLine[i] == ',')
        {
            cp[found++] = i;
        }
    }
    if (found < kCommas)
    {
        Serial.printf("[Firestore] Row %d skipped: only %d commas\n", index, found);
        return -1;
    }

    long   t_rel_ms   = field(csvLine, cp, 0,  kCols).toInt();
    long   t_rel_us   = field(csvLine, cp, 1,  kCols).toInt();
    String time_local = field(csvLine, cp, 2,  kCols);
    float  load_raw   = field(csvLine, cp, 3,  kCols).toFloat();
    float  load_filt  = field(csvLine, cp, 4,  kCols).toFloat();
    float  ax         = field(csvLine, cp, 5,  kCols).toFloat();
    float  ay         = field(csvLine, cp, 6,  kCols).toFloat();
    float  az         = field(csvLine, cp, 7,  kCols).toFloat();
    float  gx         = field(csvLine, cp, 8,  kCols).toFloat();
    float  gy         = field(csvLine, cp, 9,  kCols).toFloat();
    float  gz         = field(csvLine, cp, 10, kCols).toFloat();
    float  mx         = field(csvLine, cp, 11, kCols).toFloat();
    float  my         = field(csvLine, cp, 12, kCols).toFloat();
    float  mz         = field(csvLine, cp, 13, kCols).toFloat();
    float  roll       = field(csvLine, cp, 14, kCols).toFloat();
    float  pitch      = field(csvLine, cp, 15, kCols).toFloat();
    float  yaw        = field(csvLine, cp, 16, kCols).toFloat();

    String body =
        "{\"fields\":{"
        "\"t_rel_ms\":{\"integerValue\":\"" + String(t_rel_ms) + "\"},"
        "\"t_rel_us\":{\"integerValue\":\"" + String(t_rel_us) + "\"},"
        "\"time_local\":{\"stringValue\":\"" + time_local + "\"},"
        "\"load_cell_raw\":{\"doubleValue\":"  + String(load_raw,  3) + "},"
        "\"load_cell_filt\":{\"doubleValue\":" + String(load_filt, 3) + "},"
        "\"imu_ax\":{\"doubleValue\":"  + String(ax,  2) + "},"
        "\"imu_ay\":{\"doubleValue\":"  + String(ay,  2) + "},"
        "\"imu_az\":{\"doubleValue\":"  + String(az,  2) + "},"
        "\"imu_gx\":{\"doubleValue\":"  + String(gx,  2) + "},"
        "\"imu_gy\":{\"doubleValue\":"  + String(gy,  2) + "},"
        "\"imu_gz\":{\"doubleValue\":"  + String(gz,  2) + "},"
        "\"imu_mx\":{\"doubleValue\":"  + String(mx,  2) + "},"
        "\"imu_my\":{\"doubleValue\":"  + String(my,  2) + "},"
        "\"imu_mz\":{\"doubleValue\":"  + String(mz,  2) + "},"
        "\"roll_deg\":{\"doubleValue\":"  + String(roll,  2) + "},"
        "\"pitch_deg\":{\"doubleValue\":" + String(pitch, 2) + "},"
        "\"yaw_deg\":{\"doubleValue\":"   + String(yaw,   2) + "}"
        "}}";

    String url = String(kBaseUrl) +
                 "/sessions/" + sessionId +
                 "/readings/" + String(index) +
                 "?key=" + kApiKey;

    HTTPClient http;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int code = http.PATCH(body);
    http.end();
    return code;
}

// ---------------------------------------------------------------------------
// Main entry point
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

    Serial.printf("[Firestore] Syncing %s -> sessions/%s\n", filePath, sessionId.c_str());

    WiFiClientSecure client;
    client.setInsecure(); // self-signed / no CA pinning needed for Firestore REST

    // Session metadata doc
    int metaCode = uploadSessionDoc(client, sessionId, millis());
    Serial.printf("[Firestore] Session doc: %d\n", metaCode);
    if (metaCode != 200)
    {
        result.httpErrorCode = metaCode;
    }

    // Skip header line
    String line = f.readStringUntil('\n');

    int index = 0;
    while (f.available())
    {
        line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
        {
            continue;
        }

        int code = uploadReading(client, sessionId, index, line);
        if (code == 200)
        {
            ++result.readingsSynced;
        }
        else
        {
            Serial.printf("[Firestore] Reading %d failed: %d\n", index, code);
            result.httpErrorCode = code;
        }
        ++index;
    }
    f.close();

    Serial.printf("[Firestore] Sync complete — %d/%d readings OK\n",
                  result.readingsSynced, index);

    result.success = (result.readingsSynced == index && index > 0);
    return result;
}