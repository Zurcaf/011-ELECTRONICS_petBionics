#include "FirestoreSync.h"

#include <SD.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

<<<<<<< HEAD
// Convenience: field between comma[n-1] and comma[n].
// Pass commaPos[] pre-computed from the line.
static String field(const String &line, const int *cp, int col, int totalCols)
=======
// ---------------------------------------------------------------------------
// CSV parsing helpers
// CSV column layout (17 columns, 0-indexed):
//   0  t_rel_ms    1  t_rel_us    2  time_local
//   3  load_raw    4  load_filt
//   5  ax  6  ay  7  az  8  gx  9  gy  10  gz
//  11  mx  12  my  13  mz
//  14  roll  15  pitch  16  yaw
// ---------------------------------------------------------------------------
static String csvField(const String &line, const int *cp, int col, int totalCols)
>>>>>>> origin/main
{
    int start = (col == 0) ? 0 : cp[col - 1] + 1;
    int end   = (col < totalCols - 1) ? cp[col] : (int)line.length();
    return line.substring(start, end);
}

// ---------------------------------------------------------------------------
// Session metadata document (one PATCH, separate from batch)
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
// Build one Firestore write entry for a single CSV row.
// Appends a JSON object to `out` (no trailing comma — caller handles that).
// Returns false if the row is malformed.
// ---------------------------------------------------------------------------
bool FirestoreSync::buildWriteEntry(const String &csvLine,
                                    const String &sessionId,
                                    int index,
                                    String &out)
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
        return false;
    }

    long   t_rel_ms   = csvField(csvLine, cp,  0, kCols).toInt();
    long   t_rel_us   = csvField(csvLine, cp,  1, kCols).toInt();
    String time_local = csvField(csvLine, cp,  2, kCols);
    float  load_raw   = csvField(csvLine, cp,  3, kCols).toFloat();
    float  load_filt  = csvField(csvLine, cp,  4, kCols).toFloat();
    float  ax         = csvField(csvLine, cp,  5, kCols).toFloat();
    float  ay         = csvField(csvLine, cp,  6, kCols).toFloat();
    float  az         = csvField(csvLine, cp,  7, kCols).toFloat();
    float  gx         = csvField(csvLine, cp,  8, kCols).toFloat();
    float  gy         = csvField(csvLine, cp,  9, kCols).toFloat();
    float  gz         = csvField(csvLine, cp, 10, kCols).toFloat();
    float  mx         = csvField(csvLine, cp, 11, kCols).toFloat();
    float  my         = csvField(csvLine, cp, 12, kCols).toFloat();
    float  mz         = csvField(csvLine, cp, 13, kCols).toFloat();
    float  roll       = csvField(csvLine, cp, 14, kCols).toFloat();
    float  pitch      = csvField(csvLine, cp, 15, kCols).toFloat();
    float  yaw        = csvField(csvLine, cp, 16, kCols).toFloat();

    // Full Firestore document path for this reading
    String docPath = "projects/" FIREBASE_PROJECT "/databases/(default)/documents"
                     "/sessions/" + sessionId +
                     "/readings/" + String(index);

    out += "{\"update\":{"
           "\"name\":\"" + docPath + "\","
           "\"fields\":{"
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
           "}}}";

    return true;
}

// ---------------------------------------------------------------------------
// Send one batchWrite request.
// `writes` is a JSON array string of write objects (no surrounding brackets).
// ---------------------------------------------------------------------------
int FirestoreSync::sendBatch(WiFiClientSecure &client, const String &writes)
{
    String body = "{\"writes\":[" + writes + "]}";

    HTTPClient http;
    http.begin(client, kBatchUrl);
    http.addHeader("Content-Type", "application/json");
    // batchWrite returns 200 on full success
    int code = http.POST(body);
    if (code != 200)
    {
        String resp = http.getString();
        Serial.printf("[Firestore] batchWrite failed %d: %s\n",
                      code, resp.substring(0, 120).c_str());
    }
    http.end();
    return code;
}

// ---------------------------------------------------------------------------
// Main entry point
// Opens the CSV once, streams it in chunks of kBatchSize rows,
// sends one batchWrite per chunk. One SSL handshake for the whole session.
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
    client.setInsecure(); // No CA pinning needed for Firestore REST

    // ── 1. Session metadata doc (one PATCH) ──────────────────────────────
    int metaCode = uploadSessionDoc(client, sessionId, millis());
    Serial.printf("[Firestore] Session doc: %d\n", metaCode);
    if (metaCode != 200) result.httpErrorCode = metaCode;

    // ── 2. Skip CSV header line ───────────────────────────────────────────
    f.readStringUntil('\n');

    // ── 3. Stream CSV in batches of kBatchSize ────────────────────────────
    int globalIndex  = 0;   // absolute reading index across all batches
    int batchNumber  = 0;

    while (f.available())
    {
        // Build one batch: accumulate up to kBatchSize write entries
        String writes = "";
        int rowsInBatch = 0;

        while (f.available() && rowsInBatch < kBatchSize)
        {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            String entry = "";
            if (buildWriteEntry(line, sessionId, globalIndex, entry))
            {
                if (rowsInBatch > 0) writes += ",";
                writes += entry;
                rowsInBatch++;
                globalIndex++;
            }
        }

        if (rowsInBatch == 0) break;

        // Send the batch
        batchNumber++;
        Serial.printf("[Firestore] Sending batch %d (%d readings, index %d-%d)...\n",
                      batchNumber,
                      rowsInBatch,
                      globalIndex - rowsInBatch,
                      globalIndex - 1);

        int code = sendBatch(client, writes);
        if (code == 200)
        {
            result.readingsSynced += rowsInBatch;
            Serial.printf("[Firestore] Batch %d OK\n", batchNumber);
        }
        else
        {
            result.httpErrorCode = code;
            Serial.printf("[Firestore] Batch %d FAILED: %d\n", batchNumber, code);
            // Continue trying remaining batches — partial upload is better than none
        }
    }

    f.close();

    Serial.printf("[Firestore] Sync complete — %d/%d readings in %d batches\n",
                  result.readingsSynced, globalIndex, batchNumber);

    result.success = (result.readingsSynced == globalIndex && globalIndex > 0);
    return result;
}
