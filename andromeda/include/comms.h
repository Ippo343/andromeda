#pragma once
#include <ArduinoLog.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <DNSServer.h>
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

    // Static members for RTOS task and AP mode
    TaskHandle_t webServerTaskHandle;
    DNSServer* dnsServer;
    Preferences preferences;
    bool isAPMode;

    // Core WiFi methods
    bool connectToWiFi(const char* ssid, const char* password);
    bool startAPMode();
    bool startStationMode();
    bool testWiFiConnection(const char* ssid, const char* password);
    bool processWiFiCredentials(AsyncWebServerRequest* request, const String& ssid, const String& password);

    // Web server setup
    static void webServerTask(void* parameter);
    void createWebServerTask();
    void setupRoutes();
    void setupAPRoutes();
    void setupStationRoutes();

    // AP mode specific methods
    void serveSetupPage(AsyncWebServerRequest *request);
    String scanWiFiNetworks();
    String urlDecode(String str);
};
