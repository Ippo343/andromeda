#pragma once
#include <ArduinoLog.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>

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

    TaskHandle_t webServerTaskHandle;
    DNSServer* dnsServer;
    Preferences preferences;
    bool isAPMode;

    volatile bool scanInProgress;
    volatile bool scanComplete;
    String scanResults;
    unsigned long lastScanTime;
    static constexpr unsigned long SCAN_CACHE_MS = 30000;

    bool connectToWiFi(const char* ssid, const char* password);
    bool startAPMode();
    bool startStationMode();
    bool testWiFiConnection(const char* ssid, const char* password);
    bool processWiFiCredentials(AsyncWebServerRequest* request, const String& ssid,
                                const String& password);

    static void webServerTask(void* parameter);
    void createWebServerTask();
    void setupRoutes();

    void startAsyncScan();
    String scanWiFiNetworks();
    void onWiFiScanComplete(int networksFound);
};