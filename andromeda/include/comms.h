#pragma once

#include <ArduinoLog.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

#include "WiFi.h"
#include "mission-control.h"
#include "secrets.h"
#include "utils.h"

class Comms
{
  public:
    Comms(MissionControl& missionControl);
    bool setup();
    void printWifiStatus();

  private:
    int status = WL_IDLE_STATUS;

    MissionControl& mc;
    AsyncWebServer server;

    static TaskHandle_t webServerTaskHandle;
    static void webServerTask(void* parameter);

    void setupRoutes();
};
