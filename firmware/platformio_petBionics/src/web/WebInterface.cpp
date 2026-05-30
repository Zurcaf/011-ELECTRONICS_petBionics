#include "WebInterface.h"

// Small dashboard used to inspect the current device state and control logging.
static const char kHtmlTemplate[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="pt">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
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
  .ws-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;background:#dc3545;vertical-align:middle}
  .ws-dot.online{background:#28a745}
</style>
<script>
let socket = null;

function setClassByState(elementId, states) {
    const element = document.getElementById(elementId);
    if (!element) return;

    element.className = element.className.replace(/\b(ok|warn|fail|logging|idle|online)\b/g, '').trim();
    for (const state of states) {
        element.classList.add(state);
    }
}

function applyStatus(data) {
    const stateBadge = document.getElementById('stateBadge');
    const batteryValue = document.getElementById('batteryValue');
    const batteryClass = document.getElementById('batteryClass');
    const kgValue = document.getElementById('kgValue');
    const rollValue = document.getElementById('rollValue');
    const pitchValue = document.getElementById('pitchValue');
    const yawValue = document.getElementById('yawValue');
    const sdValue = document.getElementById('sdValue');
    const sdClass = document.getElementById('sdClass');
    const imuValue = document.getElementById('imuValue');
    const imuClass = document.getElementById('imuClass');
    const hx711Value = document.getElementById('hx711Value');
    const hx711Class = document.getElementById('hx711Class');
    const samplesValue = document.getElementById('samplesValue');
    const eventsValue = document.getElementById('eventsValue');
    const startButton = document.getElementById('startButton');
    const stopButton = document.getElementById('stopButton');

    const active = !!data.acquisitionEnabled;
    stateBadge.textContent = active ? 'A gravar' : 'Parado';
    setClassByState('stateBadge', ['badge', active ? 'logging' : 'idle']);

    batteryValue.textContent = Number(data.batteryVoltage).toFixed(2);
    setClassByState('batteryClass', [data.batteryVoltage < 3.0 ? 'fail' : (data.batteryVoltage < 3.5 ? 'warn' : 'ok')]);

    kgValue.textContent = Number(data.loadCellEstimatedKg).toFixed(3);
    rollValue.textContent = Number(data.roll).toFixed(2);
    pitchValue.textContent = Number(data.pitch).toFixed(2);
    yawValue.textContent = Number(data.yaw).toFixed(2);

    sdValue.textContent = data.sdReady ? 'OK' : 'FALHA';
    setClassByState('sdClass', [data.sdReady ? 'ok' : 'fail']);

    imuValue.textContent = data.imuReady ? 'OK' : 'FALHA';
    setClassByState('imuClass', [data.imuReady ? 'ok' : 'fail']);

    hx711Value.textContent = data.hx711Ready ? 'OK' : 'FALHA';
    setClassByState('hx711Class', [data.hx711Ready ? 'ok' : 'fail']);

    samplesValue.textContent = String(data.samples);
    eventsValue.textContent = String(data.events);

    startButton.disabled = active;
    stopButton.disabled = !active;
}

function connectWebSocket() {
    const scheme = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    socket = new WebSocket(scheme + window.location.hostname + ':81/');

    socket.addEventListener('open', () => {
        setClassByState('wsDot', ['ws-dot', 'online']);
        const wsState = document.getElementById('wsState');
        if (wsState) wsState.textContent = 'Ligado';
    });

    socket.addEventListener('message', (event) => {
        try {
            applyStatus(JSON.parse(event.data));
        } catch (error) {
            // Ignore malformed payloads.
        }
    });

    socket.addEventListener('close', () => {
        setClassByState('wsDot', ['ws-dot']);
        const wsState = document.getElementById('wsState');
        if (wsState) wsState.textContent = 'Desligado';
        window.setTimeout(connectWebSocket, 1000);
    });

    socket.addEventListener('error', () => {
        try { socket.close(); } catch (error) {}
    });
}

function formatFileName(path) {
    // Expected: /inbox/YYYYMMDD/runNNN_YYYY-MM-DD_HH-MM-SS.csv
    const parts = path.split('/').filter(Boolean);
    if (parts.length < 3) return path;
    const file = parts[2].replace('.csv', '');
    const m = file.match(/^(run\d+)_(\d{4})-(\d{2})-(\d{2})_(\d{2})-(\d{2})-(\d{2})$/);
    if (m) return m[1] + ' · ' + m[4] + '/' + m[3] + '/' + m[2] + ' ' + m[5] + ':' + m[6] + ':' + m[7];
    if (file.includes('unsynced')) return file.replace(/_unsynced_\d+$/, '') + ' · data desconhecida';
    return file;
}

async function refreshFiles() {
    try {
        const response = await fetch('/files', { cache: 'no-store' });
        if (!response.ok) return;

        const files = await response.json();
        const fileList = document.getElementById('fileList');

        if (!files.length) {
            fileList.innerHTML = '<p style="color:#999;padding:8px">Sem ficheiros</p>';
            return;
        }

        fileList.innerHTML = files.map((file) => {
            const sizeKb = Math.floor(file.size / 1024);
            const encodedFile = encodeURIComponent(file.name);
            return '<div class="file-item">' +
                '<span class="file-name">' + formatFileName(file.name) + '</span>' +
                '<span class="file-size">' + sizeKb + ' KB</span>' +
                '<a href="/download?file=' + encodedFile + '" class="download-btn">📥</a>' +
                '</div>';
        }).join('');
    } catch (error) {
        // Leave the current file list visible if a poll fails.
    }
}

window.addEventListener('DOMContentLoaded', () => {
    refreshFiles();
    setInterval(refreshFiles, 5000);
    connectWebSocket();
});
</script>
</head>
<body>
<h2>🐾 petBionic</h2>
<p style="color:#666;font-size:.8em;margin:0 0 8px 0"><span id="wsDot" class="ws-dot"></span>WebSocket <span id="wsState">Desligado</span></p>
<div class="card">
    <div class="row"><span>Estado</span><span id="stateBadge" class="badge %BADGE%">%STATE%</span></div>
    <div class="row"><span>Bateria</span><span id="batteryClass" class="%BATT_CLASS%"><b id="batteryValue">%BATT_V%</b> V</span></div>
    <div class="row"><span>Kg</span><span><b id="kgValue">%KG%</b> kg</span></div>
    <div class="row"><span>Roll</span><span><b id="rollValue">%ROLL%</b> °</span></div>
    <div class="row"><span>Pitch</span><span><b id="pitchValue">%PITCH%</b> °</span></div>
    <div class="row"><span>Yaw</span><span><b id="yawValue">%YAW%</b> °</span></div>
    <div class="row"><span>SD Card</span><span id="sdClass" class="%SD_CLASS%"><span id="sdValue">%SD%</span></span></div>
    <div class="row"><span>IMU</span><span id="imuClass" class="%IMU_CLASS%"><span id="imuValue">%IMU%</span></span></div>
    <div class="row"><span>HX711</span><span id="hx711Class" class="%HX711_CLASS%"><span id="hx711Value">%HX711%</span></span></div>
    <div class="row"><span>Amostras</span><span><b id="samplesValue">%SAMPLES%</b></span></div>
    <div class="row"><span>Eventos</span><span><b id="eventsValue">%EVENTS%</b></span></div>
</div>
<div class="actions">
  <form method="POST" action="/start" style="flex:1;display:flex">
        <button id="startButton" class="btn-start" %START_DIS%>&#9654; Iniciar</button>
  </form>
  <form method="POST" action="/stop" style="flex:1;display:flex">
        <button id="stopButton" class="btn-stop" %STOP_DIS%>&#9632; Parar</button>
  </form>
</div>
<div class="card">
  <h3 style="margin:0 0 12px 0">📥 Ficheiros (SD)</h3>
    <div id="fileList" class="file-list">%FILES%</div>
</div>
<p style="color:#999;font-size:.75em;text-align:center">Valores atualizados 10x por segundo</p>
</body>
</html>)html";

// ---------------------------------------------------------------------------

WebInterface::WebInterface(AppConfig &config, AppStatus &status, RawSdLogger &logger)
    : _server(80), _config(config), _status(status), _logger(logger), _webSocket(81), _lastStatusBroadcastMs(0), _lastStatusPayload()
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

    _webSocket.begin();
    _webSocket.onEvent([this](uint8_t num, WStype_t type, uint8_t *payload, size_t length)
                      {
        (void)num;
        (void)payload;
        (void)length;

        if (type == WStype_CONNECTED)
        {
            broadcastStatus(true);
        }
    });

    Serial.println("[Web] HTTP server started on port 80");
    Serial.println("[Web] WebSocket server started on port 81");
}

void WebInterface::update()
{
    _server.handleClient();
    _webSocket.loop();
    broadcastStatus(false);
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

    const char *rawReqPath = requestedFilePath.c_str();
    const char *lastSlash = strrchr(rawReqPath, '/');
    String downloadBaseName = lastSlash ? String(lastSlash + 1) : requestedFilePath;
    _server.sendHeader("Content-Disposition", "attachment; filename=\"" + downloadBaseName + "\"");
    _server.setContentLength(requestedFileSize);
    _server.send(200, "text/csv", "");

    // Stream the file via RawSdLogger using its configured SPI instance
    const size_t kDownloadChunkSize = 2048;
    uint8_t chunkBuffer[kDownloadChunkSize];
    size_t bytesSent = 0;

    while (bytesSent < requestedFileSize)
    {
        size_t bytesToRead = (requestedFileSize - bytesSent > kDownloadChunkSize) ? kDownloadChunkSize : (requestedFileSize - bytesSent);
        size_t bytesRead = 0;

        if (!_logger.readFileAt(requestedFilePath.c_str(), bytesSent, chunkBuffer, bytesToRead, bytesRead))
        {
            Serial.println("[Download] Could not read file");
            break;
        }

        if (bytesRead == 0)
            break;

        _server.client().write(chunkBuffer, bytesRead);
        bytesSent += bytesRead;
    }

    Serial.printf("[Download] Complete: %zu / %zu bytes\n", bytesSent, requestedFileSize);
}

void WebInterface::broadcastStatus(bool force)
{
    const uint32_t nowMs = millis();
    if (!force && (nowMs - _lastStatusBroadcastMs) < 100)
    {
        return;
    }

    _lastStatusBroadcastMs = nowMs;
    String payload = buildStatusJson();
    if (!force && payload == _lastStatusPayload)
    {
        return;
    }

    _lastStatusPayload = payload;
    _webSocket.broadcastTXT(payload);
}

String WebInterface::buildStatusJson() const
{
    char responseBody[256];
    snprintf(responseBody, sizeof(responseBody),
             "{\"acquisitionEnabled\":%s,\"sdReady\":%s,\"imuReady\":%s,\"hx711Ready\":%s,\"samples\":%lu,\"events\":%lu,\"batteryVoltage\":%.2f,\"loadCellEstimatedKg\":%.3f,\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f}",
             _config.acquisitionEnabled ? "true" : "false",
             _status.sdReady ? "true" : "false",
             _status.imuReady ? "true" : "false",
             _status.hx711Ready ? "true" : "false",
             static_cast<unsigned long>(_status.samples),
             static_cast<unsigned long>(_status.events),
             _status.batteryVoltage,
             _status.loadCellEstimatedKg,
             _status.roll,
             _status.pitch,
             _status.yaw);

    return String(responseBody);
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
    html.replace("%KG%", String(_status.loadCellEstimatedKg, 3));
    html.replace("%ROLL%", String(_status.roll, 2));
    html.replace("%PITCH%", String(_status.pitch, 2));
    html.replace("%YAW%", String(_status.yaw, 2));

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

    html.replace("%FILES%", "<p style=\"color:#999;padding:8px\">A carregar...</p>");

    return html;
}
