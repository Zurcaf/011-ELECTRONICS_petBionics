#include "WebInterface.h"

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
<p style="color:#999;font-size:.75em;text-align:center">Atualiza automaticamente a cada 3s</p>
</body>
</html>)html";

// ---------------------------------------------------------------------------

WebInterface::WebInterface(AppConfig &config, AppStatus &status)
    : _server(80), _config(config), _status(status)
{
}

void WebInterface::begin()
{
    _server.on("/",      HTTP_GET,  [this]() { handleRoot();  });
    _server.on("/start", HTTP_POST, [this]() { handleStart(); });
    _server.on("/stop",  HTTP_POST, [this]() { handleStop();  });
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

String WebInterface::buildHtml() const
{
    const bool logging = _config.acquisitionEnabled;

    // Battery status: green ≥3.5V, yellow 3.0-3.5V, red <3.0V (for 3.7V nominal)
    String battClass = "ok";
    if (_status.batteryVoltage < 3.0f)
        battClass = "fail";
    else if (_status.batteryVoltage < 3.5f)
        battClass = "warn";

    String html = FPSTR(kHtmlTemplate);

    html.replace("%STATE%",    logging ? "A gravar" : "Parado");
    html.replace("%BADGE%",    logging ? "logging"  : "idle");

    char battStr[16];
    snprintf(battStr, sizeof(battStr), "%.2f", _status.batteryVoltage);
    html.replace("%BATT_V%",      battStr);
    html.replace("%BATT_CLASS%",  battClass);

    html.replace("%SD_CLASS%",    _status.sdReady    ? "ok" : "fail");
    html.replace("%SD%",          _status.sdReady    ? "OK" : "FALHA");
    html.replace("%IMU_CLASS%",   _status.imuReady   ? "ok" : "fail");
    html.replace("%IMU%",         _status.imuReady   ? "OK" : "FALHA");
    html.replace("%HX711_CLASS%", _status.hx711Ready ? "ok" : "fail");
    html.replace("%HX711%",       _status.hx711Ready ? "OK" : "FALHA");

    html.replace("%SAMPLES%", String(_status.samples));
    html.replace("%EVENTS%",  String(_status.events));

    html.replace("%START_DIS%", logging ? "disabled" : "");
    html.replace("%STOP_DIS%",  logging ? ""         : "disabled");

    return html;
}
