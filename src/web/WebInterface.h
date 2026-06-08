#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "../core/AppConfig.h"
#include "../core/AppTypes.h"
#include "../storage/RawSdLogger.h"

class WebInterface
{
public:
    WebInterface(AppConfig &config, AppStatus &status, RawSdLogger &logger);
    void begin();
    void update();

private:
    WebServer _server;
    AppConfig &_config;
    AppStatus &_status;
    RawSdLogger &_logger;
    WebSocketsServer _webSocket;
    uint32_t _lastStatusBroadcastMs;
    String _lastStatusPayload;

    void handleRoot();
    void handleStart();
    void handleStop();
    void handleStatus();
    void handleFiles();
    void handleDownload();

    void broadcastStatus(bool force = false);
    String buildStatusJson() const;
    String buildHtml() const;
};
