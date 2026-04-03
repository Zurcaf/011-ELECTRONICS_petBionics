#include "FirestoreSync.h"

#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace
{
  String basenameFromPath(const String &path)
  {
    int slash = path.lastIndexOf('/');
    if (slash < 0)
    {
      return path;
    }
    return path.substring(slash + 1);
  }

  String removeCsvExt(const String &fileName)
  {
    if (!fileName.endsWith(".csv"))
    {
      return fileName;
    }

    return fileName.substring(0, fileName.length() - 4);
  }
}

FirestoreSync::FirestoreSync()
    : _projectId(PETBIONICS_FIREBASE_PROJECT_ID),
      _apiKey(PETBIONICS_FIREBASE_WEB_API_KEY)
{
}

void FirestoreSync::begin()
{
  Serial.printf("[Cloud] begin project='%s' api_key_len=%u\n",
                _projectId.c_str(),
                static_cast<unsigned>(_apiKey.length()));
  if (!isConfigured())
  {
    Serial.println("Firestore: set PETBIONICS_FIREBASE_PROJECT_ID and PETBIONICS_FIREBASE_WEB_API_KEY in FirestoreSync.h");
  }
}

bool FirestoreSync::isConfigured() const
{
  return _projectId.length() > 0 && _apiKey.length() > 0;
}

bool FirestoreSync::syncCsvFile(const String &filePath, const String &deviceId)
{
  Serial.printf("[Cloud] syncCsvFile path='%s' device='%s'\n", filePath.c_str(), deviceId.c_str());
  if (!isConfigured())
  {
    Serial.println("Firestore: missing compile-time project/api key");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Firestore: wifi disconnected");
    return false;
  }

  File file = SD.open(filePath, FILE_READ);
  if (!file)
  {
    Serial.printf("Firestore: failed to open %s\n", filePath.c_str());
    return false;
  }

  String sessionId = removeCsvExt(basenameFromPath(filePath));
  Serial.printf("[Cloud] sessionId='%s'\n", sessionId.c_str());
  String sessionBody = String("{\"fields\":{") +
                       "\"sessionId\":{\"stringValue\":\"" + escapeJson(sessionId) + "\"}," +
                       "\"device\":{\"stringValue\":\"" + escapeJson(deviceId) + "\"}," +
                       "\"csvPath\":{\"stringValue\":\"" + escapeJson(filePath) + "\"}" +
                       "}}";

  if (!postDoc("sessions/" + sessionId, sessionBody))
  {
    file.close();
    return false;
  }

  uint32_t uploaded = 0;
  bool firstLine = true;

  while (file.available())
  {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() == 0)
    {
      continue;
    }

    if (firstLine)
    {
      firstLine = false;
      continue;
    }

    String readingBody = String("{\"fields\":{") +
                         "\"raw\":{\"stringValue\":\"" + escapeJson(line) + "\"}" +
                         "}}";

    String readingId = String(uploaded);
    if (uploaded == 0)
    {
      Serial.printf("[Cloud] uploading first row from %s\n", filePath.c_str());
    }
    if (!postDoc("sessions/" + sessionId + "/readings/" + readingId, readingBody))
    {
      file.close();
      return false;
    }

    ++uploaded;
    delay(10);
  }

  file.close();
  Serial.printf("Firestore: synced %lu rows from %s\n",
                static_cast<unsigned long>(uploaded),
                filePath.c_str());

  return true;
}

bool FirestoreSync::syncPendingCsvFiles(const String &localRoot,
                                        const String &sentRoot,
                                        const String &deviceId,
                                        uint32_t &syncedCount)
{
  syncedCount = 0;

  Serial.printf("[Cloud] syncPendingCsvFiles localRoot='%s' sentRoot='%s' device='%s'\n",
                localRoot.c_str(),
                sentRoot.c_str(),
                deviceId.c_str());

  if (!isConfigured())
  {
    Serial.println("Firestore: missing compile-time config");
    return false;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Firestore: wifi disconnected");
    return false;
  }

  if (!ensureDirectoryPath(localRoot))
  {
    Serial.printf("Firestore: cannot access %s\n", localRoot.c_str());
    return false;
  }

  if (!ensureDirectoryPath(sentRoot))
  {
    Serial.printf("Firestore: cannot access %s\n", sentRoot.c_str());
    return false;
  }

  bool ok = syncDirectoryRecursive(localRoot, localRoot, sentRoot, deviceId, syncedCount);
  Serial.printf("[Cloud] syncPendingCsvFiles done ok=%s count=%lu\n",
                ok ? "true" : "false",
                static_cast<unsigned long>(syncedCount));
  return ok;
}

bool FirestoreSync::syncDirectoryRecursive(const String &currentDir,
                                           const String &localRoot,
                                           const String &sentRoot,
                                           const String &deviceId,
                                           uint32_t &syncedCount)
{
  File dir = SD.open(currentDir, FILE_READ);
  if (!dir || !dir.isDirectory())
  {
    if (dir)
    {
      dir.close();
    }
    return false;
  }

  bool ok = true;
  File entry = dir.openNextFile();
  while (entry)
  {
    String entryPath = String(entry.name());
    bool isDir = entry.isDirectory();
    entry.close();

    Serial.printf("[Cloud] inspect '%s' dir=%s\n", entryPath.c_str(), isDir ? "true" : "false");

    if (isDir)
    {
      if (!syncDirectoryRecursive(entryPath, localRoot, sentRoot, deviceId, syncedCount))
      {
        ok = false;
        break;
      }
    }
    else if (hasCsvExtension(entryPath))
    {
      Serial.printf("[Cloud] uploading pending file '%s'\n", entryPath.c_str());
      if (!syncCsvFile(entryPath, deviceId))
      {
        ok = false;
        break;
      }

      if (!moveFileToSentTree(localRoot, sentRoot, entryPath))
      {
        ok = false;
        break;
      }

      Serial.printf("[Cloud] moved uploaded file to sent tree '%s'\n", entryPath.c_str());
      ++syncedCount;
    }

    entry = dir.openNextFile();
  }

  dir.close();
  return ok;
}

bool FirestoreSync::moveFileToSentTree(const String &localRoot, const String &sentRoot, const String &sourcePath)
{
  Serial.printf("[Cloud] moveFileToSentTree source='%s'\n", sourcePath.c_str());
  if (!sourcePath.startsWith(localRoot))
  {
    Serial.printf("Firestore: source outside local tree: %s\n", sourcePath.c_str());
    return false;
  }

  String relativePath = sourcePath.substring(localRoot.length());
  String destinationPath = sentRoot + relativePath;

  int slash = destinationPath.lastIndexOf('/');
  if (slash <= 0)
  {
    return false;
  }

  String parentPath = destinationPath.substring(0, slash);
  if (!ensureDirectoryPath(parentPath))
  {
    Serial.printf("Firestore: failed creating sent folder %s\n", parentPath.c_str());
    return false;
  }

  if (SD.exists(destinationPath))
  {
    Serial.printf("[Cloud] removing existing destination '%s'\n", destinationPath.c_str());
    SD.remove(destinationPath);
  }

  if (!SD.rename(sourcePath, destinationPath))
  {
    Serial.printf("Firestore: failed moving %s -> %s\n", sourcePath.c_str(), destinationPath.c_str());
    return false;
  }

  Serial.printf("[Cloud] moved '%s' -> '%s'\n", sourcePath.c_str(), destinationPath.c_str());
  return true;
}

bool FirestoreSync::hasCsvExtension(const String &path)
{
  return path.endsWith(".csv");
}

bool FirestoreSync::ensureDirectoryPath(const String &path)
{
  Serial.printf("[Cloud] ensureDirectoryPath '%s'\n", path.c_str());
  if (path.length() == 0 || path[0] != '/')
  {
    return false;
  }

  if (SD.exists(path))
  {
    File dir = SD.open(path, FILE_READ);
    bool isDir = dir && dir.isDirectory();
    if (dir)
    {
      dir.close();
    }
    return isDir;
  }

  String current;
  for (size_t i = 1; i < path.length(); ++i)
  {
    if (path[i] != '/')
    {
      continue;
    }

    current = path.substring(0, i);
    if (current.length() == 0)
    {
      continue;
    }

    if (!SD.exists(current) && !SD.mkdir(current))
    {
      return false;
    }
  }

  return SD.exists(path) || SD.mkdir(path);
}

bool FirestoreSync::postDoc(const String &docPath, const String &jsonBody)
{
  Serial.printf("[Cloud] postDoc '%s' body_len=%u\n",
                docPath.c_str(),
                static_cast<unsigned>(jsonBody.length()));
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String("https://firestore.googleapis.com/v1/projects/") +
               _projectId +
               "/databases/(default)/documents/" +
               docPath +
               "?key=" +
               _apiKey;

  if (!http.begin(client, url))
  {
    Serial.println("Firestore: HTTP begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.PATCH(jsonBody);
  Serial.printf("[Cloud] HTTP PATCH code=%d\n", code);

  if (code != 200)
  {
    Serial.printf("Firestore: HTTP %d body=%s\n", code, http.getString().c_str());
    http.end();
    return false;
  }

  http.end();
  return true;
}

String FirestoreSync::escapeJson(const String &value)
{
  String out;
  out.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i)
  {
    const char c = value[i];
    if (c == '\\')
    {
      out += "\\\\";
    }
    else if (c == '"')
    {
      out += "\\\"";
    }
    else
    {
      out += c;
    }
  }

  return out;
}
