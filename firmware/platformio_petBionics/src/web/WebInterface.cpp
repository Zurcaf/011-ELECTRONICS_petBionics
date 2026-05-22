#include "WebInterface.h"

// Small dashboard used to inspect the current device state and control logging.
static const char kHtmlTemplate[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="pt">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="3">
<title>petBionic</title>
<style>
  body{font-family:sans-serif;max-width:420px;margin:40px auto;padding:0 16px;background:#fafafa}
  h2{margin-bottom:4px}
  .card{background:#fff;border:1px solid #ddd;border-radius:10px;padding:16px;margin:16px 0}
  .row{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid #f0f0f0}
  .row:last-child{border-bottom:none}
  .ok{color:#28a745;font-weight:600}
  .fail{color:#dc3545;font-weight:600}
  .warn{color:#ff8c00;font-weight:600}
  .badge{display:inline-block;padding:3px 12px;border-radius:12px;font-size:.85em;font-weight:600}
  .logging{background:#d4edda;color:#155724}
  .idle{background:#f8d7da;color:#721c24}
  .actions{display:flex;gap:10px;margin-top:8px}
  button{flex:1;padding:14px;font-size:1em;border:none;border-radius:8px;cursor:pointer;font-weight:600}
  .btn-start{background:#28a745;color:#fff}
  .btn-stop{background:#dc3545;color:#fff}
  button:disabled{opacity:.35;cursor:default}
  .file-list{max-height:300px;overflow-y:auto;font-size:.9em}
  .file-item{display:flex;justify-content:space-between;align-items:center;padding:8px;border-bottom:1px solid #f0f0f0}
  .file-item:last-child{border-bottom:none}
  .file-name{flex:1;word-break:break-all}
  .file-size{color:#999;margin:0 8px;font-size:.85em}
  .download-btn{background:#007bff;color:#fff;padding:4px 12px;text-decoration:none;border-radius:4px;font-size:.85em;white-space:nowrap;margin-left:8px}
</style>
</head>
<body>
<h2>🐾 petBionic</h2>
<div class="card">
  <div class="row"><span>Estado</span><span class="badge %BADGE%">%STATE%</span></div>
  <div class="row"><span>Bateria</span><span class="%BATT_CLASS%"><b>%BATT_V%</b> V</span></div>
  <div class="row"><span>SD Card</span><span class="%SD_CLASS%">%SD%</span></div>
  <div class="row"><span>IMU</span><span class="%IMU_CLASS%">%IMU%</span></div>
  <div class="row"><span>HX711</span><span class="%HX711_CLASS%">%HX711%</span></div>
  <div class="row"><span>Amostras</span><span><b>%SAMPLES%</b></span></div>
  <div class="row"><span>Eventos</span><span><b>%EVENTS%</b></span></div>
</div>
<div class="actions">
  <form method="POST" action="/start" style="flex:1;display:flex">
    <button class="btn-start" %START_DIS%>&#9654; Iniciar</button>
  </form>
  <form method="POST" action="/stop" style="flex:1;display:flex">
    <button class="btn-stop" %STOP_DIS%>&#9632; Parar</button>
  </form>
</div>
<div class="card">
  <h3 style="margin:0 0 12px 0">📥 Ficheiros (SD)</h3>
  <div class="file-list">%FILES%</div>
</div>
<p style="color:#999;font-size:.75em;text-align:center">Atualiza automaticamente a cada 3s</p>
</body>
</html>)html";

// ---------------------------------------------------------------------------

WebInterface::WebInterface(AppConfig &config, AppStatus &status, RawSdLogger &logger)
    : _server(80), _config(config), _status(status), _logger(logger)
{
}

void WebInterface::begin()
{
    // Route layout: dashboard, start/stop logging, list files, download files.
    _server.on("/", HTTP_GET, [this]()
               { handleRoot(); });
    _server.on("/start", HTTP_POST, [this]()
               { handleStart(); });
    _server.on("/stop", HTTP_POST, [this]()
               { handleStop(); });
    _server.on("/files", HTTP_GET, [this]()
               { handleFiles(); });
    _server.on("/download", HTTP_GET, [this]()
               { handleDownload(); });
    _server.begin();
    Serial.println("[Web] HTTP server started on port 80");
}

void WebInterface::update()
{
    _server.handleClient();
}

void WebInterface::handleRoot()
{
    _server.send(200, "text/html", buildHtml());
}

void WebInterface::handleStart()
{
    _config.acquisitionEnabled = true;
    _server.sendHeader("Location", "/");
    _server.send(303);
}

void WebInterface::handleStop()
{
    _config.acquisitionEnabled = false;
    _server.sendHeader("Location", "/");
    _server.send(303);
}

void WebInterface::handleFiles()
{
    const int kMaxFilesToList = 50;
    String filePaths[kMaxFilesToList];
    int fileCount = _logger.listFiles(filePaths, kMaxFilesToList);

    String responseBody = "[";
    for (int i = 0; i < fileCount; i++)
    {
        if (i > 0)
            responseBody += ",";
        size_t fileSizeBytes = _logger.getFileSize(filePaths[i].c_str());
        responseBody += "{\"name\":\"" + filePaths[i] + "\",\"size\":" + String(fileSizeBytes) + "}";
    }
    responseBody += "]";

    _server.send(200, "application/json", responseBody);
}

void WebInterface::handleDownload()
{
    if (!_server.hasArg("file"))
    {
        _server.send(400, "text/plain", "Missing 'file' parameter");
        return;
    }

    String requestedFilePath = _server.arg("file");
    Serial.printf("[Download] Requested: %s\n", requestedFilePath.c_str());

    size_t requestedFileSize = _logger.getFileSize(requestedFilePath.c_str());
    Serial.printf("[Download] File size: %zu bytes\n", requestedFileSize);

    if (requestedFileSize == 0)
    {
        Serial.println("[Download] File not found or empty");
        _server.send(404, "text/plain", "File not found");
        return;
    }

    _server.sendHeader("Content-Disposition", "attachment; filename=\"" + requestedFilePath + "\"");
    _server.setContentLength(requestedFileSize);
    _server.send(200, "text/csv", "");

    // Stream the file in small chunks so the HTTP response stays responsive.
    // Open the file once and stream sequentially to avoid re-reading the
    // beginning of the file on each chunk (which caused duplicated blocks).
    const size_t kDownloadChunkSize = 2048;
    uint8_t chunkBuffer[kDownloadChunkSize];
    size_t bytesSent = 0;

    File file = SD.open(requestedFilePath.c_str(), FILE_READ);
    if (!file)
    {
        Serial.println("[Download] Could not open file for streaming");
    }
    else
    {
        while (bytesSent < requestedFileSize)
        {
            size_t bytesToRead = (requestedFileSize - bytesSent > kDownloadChunkSize) ? kDownloadChunkSize : (requestedFileSize - bytesSent);
            size_t bytesRead = file.read(chunkBuffer, bytesToRead);
            if (bytesRead == 0)
                break;

            _server.client().write(chunkBuffer, bytesRead);
            bytesSent += bytesRead;
        }
        file.close();
    }

    Serial.printf("[Download] Complete: %zu / %zu bytes\n", bytesSent, requestedFileSize);
}

String WebInterface::buildHtml() const
{
    const bool isAcquisitionActive = _config.acquisitionEnabled;

    // Battery status
    String batteryClass = "ok";
    if (_status.batteryVoltage < 3.0f)
        batteryClass = "fail";
    else if (_status.batteryVoltage < 3.5f)
        batteryClass = "warn";

    String html = FPSTR(kHtmlTemplate);

    html.replace("%STATE%", isAcquisitionActive ? "A gravar" : "Parado");
    html.replace("%BADGE%", isAcquisitionActive ? "logging" : "idle");

    char battStr[16];
    snprintf(battStr, sizeof(battStr), "%.2f", _status.batteryVoltage);
    html.replace("%BATT_V%", battStr);
    html.replace("%BATT_CLASS%", batteryClass);

    html.replace("%SD_CLASS%", _status.sdReady ? "ok" : "fail");
    html.replace("%SD%", _status.sdReady ? "OK" : "FALHA");
    html.replace("%IMU_CLASS%", _status.imuReady ? "ok" : "fail");
    html.replace("%IMU%", _status.imuReady ? "OK" : "FALHA");
    html.replace("%HX711_CLASS%", _status.hx711Ready ? "ok" : "fail");
    html.replace("%HX711%", _status.hx711Ready ? "OK" : "FALHA");

    html.replace("%SAMPLES%", String(_status.samples));
    html.replace("%EVENTS%", String(_status.events));

    html.replace("%START_DIS%", isAcquisitionActive ? "disabled" : "");
    html.replace("%STOP_DIS%", isAcquisitionActive ? "" : "disabled");

    // Build file list HTML
    const int kMaxFilesToList = 50;
    String filePaths[kMaxFilesToList];
    int fileCount = _logger.listFiles(filePaths, kMaxFilesToList);

    String fileListHtml = "";
    if (fileCount == 0)
    {
        fileListHtml = "<p style=\"color:#999;padding:8px\">Sem ficheiros</p>";
    }
    else
    {
        for (int i = 0; i < fileCount; i++)
        {
            size_t fileSizeBytes = _logger.getFileSize(filePaths[i].c_str());
            fileListHtml += "<div class=\"file-item\">";
            fileListHtml += "<span class=\"file-name\">" + filePaths[i] + "</span>";
            fileListHtml += "<span class=\"file-size\">" + String(fileSizeBytes / 1024) + " KB</span>";
            fileListHtml += "<a href=\"/download?file=" + filePaths[i] + "\" class=\"download-btn\">📥</a>";
            fileListHtml += "</div>";
        }
    }
    html.replace("%FILES%", fileListHtml);

    return html;
}
