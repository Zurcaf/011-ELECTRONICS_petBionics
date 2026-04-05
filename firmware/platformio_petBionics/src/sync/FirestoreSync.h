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
    static constexpr int kBatchSize = 100;

    static constexpr const char *kApiKey  = FIRESTORE_API_KEY;
    static constexpr const char *kBaseUrl =
        "https://firestore.googleapis.com/v1/projects/" FIREBASE_PROJECT
        "/databases/(default)/documents";
    static constexpr const char *kBatchUrl =
        "https://firestore.googleapis.com/v1/projects/" FIREBASE_PROJECT
        "/databases/(default)/documents:batchWrite"
        "?key=" FIRESTORE_API_KEY;

    int  uploadSessionDoc(WiFiClientSecure &client,
                          const String &sessionId,
                          uint32_t startMs);
    bool buildWriteEntry(const String &csvLine,
                         const String &sessionId,
                         int index,
                         String &out);
    int  sendBatch(WiFiClientSecure &client, const String &writes);
};
