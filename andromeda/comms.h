#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <ArduinoLog.h>
#include <WiFiS3.h>
#include "utils.h"
#include "mission-control.h"
#include "secrets.h"

extern IPAddress local_IP;
extern IPAddress gateway;
extern IPAddress subnet;
extern IPAddress primaryDNS;
extern IPAddress secondaryDNS;
extern const byte port;

class Comms
{
  private:
    int status = WL_IDLE_STATUS;
    WiFiServer server;

    MissionControl& mc;

    static constexpr size_t REQUEST_LINE_BUFFER_SIZE = 64;
    static constexpr unsigned long REQUEST_TIMEOUT_MS = 100;  // Increased timeout for reliability

  public:
    Comms(MissionControl& missionControl);

    bool setup();

    inline void printWifiStatus();

    void loop();

  private:
    bool readRequestLine(WiFiClient& client, char* buffer, size_t bufferSize);

    void handleRequest(const char* line);

    void reply(WiFiClient& client);
};

#endif