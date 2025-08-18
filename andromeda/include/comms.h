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

    static inline Comms& Instance()
    {
      static Comms instance;
      return instance;
    }

    Comms(const Comms&) = delete;
    Comms& operator=(const Comms&) = delete;

    bool setup();
    void printWifiStatus();

  private:

    Comms();

    int status = WL_IDLE_STATUS;

    AsyncWebServer server;
    static TaskHandle_t webServerTaskHandle;
    static void webServerTask(void* parameter);

    void setupRoutes();
};
