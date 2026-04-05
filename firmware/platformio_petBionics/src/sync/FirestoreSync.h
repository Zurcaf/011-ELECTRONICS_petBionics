#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>   // ADD THIS LINE

struct SyncResult
{
    bool success;
    int  readingsSynced;
    int  httpErrorCode;   // last non-200 code, or 0 if all OK
};

class FirestoreSync
{
public:
    // Reads a CSV file from the SD card at `filePath`, uploads it to
    // Firestore under sessions/{sessionId}, then calls sdLogger.markAsSent().
    // Returns a SyncResult describing the outcome.
    //
    // The CSV is expected to have this header (friend's RawSdLogger format):
    //   t_rel_ms, t_rel_us, time_local, load_cell_raw, load_cell_filt,
    //   imu_ax, imu_ay, imu_az, imu_gx, imu_gy, imu_gz,
    //   imu_mx, imu_my, imu_mz, roll_deg, pitch_deg, yaw_deg
    SyncResult syncFile(const char *filePath, const String &sessionId);

private:
    static constexpr const char *kProject   = "petbionic-71360";
    static constexpr const char *kApiKey    = "AIzaSyBdueBd5STH_rrrZxo59i-6CMR-ozFgyV0";
    static constexpr const char *kBaseUrl   =
        "https://firestore.googleapis.com/v1/projects/petbionic-71360"
        "/databases/(default)/documents";

    // Upload the session-level metadata document.
    // Returns the HTTP response code (200 = OK).
    int uploadSessionDoc(WiFiClientSecure &client,
                         const String &sessionId,
                         uint32_t startMs);

    // Upload one reading row. Returns HTTP response code.
    int uploadReading(WiFiClientSecure &client,
                      const String &sessionId,
                      int index,
                      const String &csvLine);
};