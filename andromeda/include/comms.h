#pragma once

#include <ArduinoLog.h>
#include "utils.h"
#include "mission-control.h"
#include "secrets.h"

#ifdef ARDUINO_R4_WIFI
#include "WiFiS3.h"
#endif

#ifdef ESP32
#include "WiFi.h"
#include <ESPAsyncWebServer.h>
#endif

class Comms
{
  private:
    int status = WL_IDLE_STATUS;
    MissionControl& mc;

#ifdef ARDUINO_R4_WIFI
    WiFiServer server;
    static constexpr size_t REQUEST_LINE_BUFFER_SIZE = 64;
    static constexpr unsigned long REQUEST_TIMEOUT_MS = 100;  // Increased timeout for reliability
#endif

#ifdef ESP32
    AsyncWebServer server;
#endif

  public:
    Comms(MissionControl& missionControl);
    bool setup();
    void printWifiStatus();
    void loop();

  private:
#ifdef ARDUINO_R4_WIFI
    bool readRequestLine(WiFiClient& client, char* buffer, size_t bufferSize);
    void handleRequest(const char* line);
    void reply(WiFiClient& client);
#endif

#ifdef ESP32
    void sendMainPage(AsyncWebServerRequest *request);
#endif
};
