#pragma once

#include <Arduino.h>
#include <WebServer.h>
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

    void handleRoot();
    void handleStart();
    void handleStop();
    void handleFiles();
    void handleDownload();

    String buildHtml() const;
};
