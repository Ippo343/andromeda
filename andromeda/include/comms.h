#pragma once

#include "WiFi.h"
#include "mission-control.h"
#include "secrets.h"
#include "utils.h"
#include <ArduinoLog.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

class Comms
{
  private:
    int status = WL_IDLE_STATUS;
    MissionControl& mc;
    AsyncWebServer server;

  public:
    Comms(MissionControl& missionControl);
    bool setup();
    void printWifiStatus();
    void loop();

  private:
    void sendMainPage(AsyncWebServerRequest *request);
    static TaskHandle_t webServerTaskHandle;
    static void webServerTask(void* parameter);
    void setupRoutes();
};
