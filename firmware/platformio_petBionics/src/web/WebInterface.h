#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "../core/AppConfig.h"
#include "../core/AppTypes.h"

class WebInterface
{
public:
    WebInterface(AppConfig &config, AppStatus &status);
    void begin();
    void update();

private:
    WebServer _server;
    AppConfig &_config;
    AppStatus &_status;

    void handleRoot();
    void handleStart();
    void handleStop();

    String buildHtml() const;
};
