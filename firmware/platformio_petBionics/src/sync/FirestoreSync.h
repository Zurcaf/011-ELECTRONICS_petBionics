#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

struct SyncResult
{
    bool success;
    int  readingsSynced;
    int  httpErrorCode;
};

class FirestoreSync
{
public:
    SyncResult syncFile(const char *filePath, const String &sessionId);

private:
    static constexpr const char *kApiKey  = FIRESTORE_API_KEY;
    static constexpr const char *kBaseUrl =
        "https://firestore.googleapis.com/v1/projects/" FIREBASE_PROJECT
        "/databases/(default)/documents";

    int uploadSessionDoc(WiFiClientSecure &client,
                         const String &sessionId,
                         uint32_t startMs);
    int uploadReading(WiFiClientSecure &client,
                      const String &sessionId,
                      int index,
                      const String &csvLine);
};
