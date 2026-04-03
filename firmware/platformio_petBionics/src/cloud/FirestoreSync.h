#pragma once

#include <Arduino.h>

#ifndef PETBIONICS_FIREBASE_PROJECT_ID
#define PETBIONICS_FIREBASE_PROJECT_ID "iot-alarm-app"
#endif

#ifndef PETBIONICS_FIREBASE_WEB_API_KEY
#define PETBIONICS_FIREBASE_WEB_API_KEY "AIzaSyAM5T2Gcq0CIZxUgMjVY08popnff-YvpNE"
#endif

class FirestoreSync
{
public:
  FirestoreSync();

  void begin();
  bool isConfigured() const;

  bool syncCsvFile(const String &filePath, const String &deviceId);
  bool syncPendingCsvFiles(const String &localRoot,
                           const String &sentRoot,
                           const String &deviceId,
                           uint32_t &syncedCount);

private:
  String _projectId;
  String _apiKey;

  bool postDoc(const String &docPath, const String &jsonBody);
  bool moveFileToSentTree(const String &localRoot, const String &sentRoot, const String &sourcePath);
  bool syncDirectoryRecursive(const String &currentDir,
                              const String &localRoot,
                              const String &sentRoot,
                              const String &deviceId,
                              uint32_t &syncedCount);
  static bool hasCsvExtension(const String &path);
  static bool ensureDirectoryPath(const String &path);
  static String escapeJson(const String &value);
};
