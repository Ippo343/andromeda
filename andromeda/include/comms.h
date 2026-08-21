#pragma once
#include <ArduinoLog.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

#include "WiFi.h"
#include "comms-utils.h"
#include "mission-control.h"
#include "secrets.h"
#include "utils.h"
#include "wifi-esp-adapters.h"
#include "wifi-manager.h"
#include "ws-state-builder.h"

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

    AsyncWebServer server;
    AsyncWebSocket ws;

    TaskHandle_t webServerTaskHandle;
    DNSServer* dnsServer;
    bool isAPMode;

    EspWiFiConnector wifiConnector;
    EspPreferencesStore preferencesStore;
    WifiManager realWifiManager;

    // Always points at realWifiManager in production. Native tests
    // (CommsTestAccess) repoint this at a WifiManager built from fake
    // IWiFiConnector/IPreferencesStore, so route handlers exercise real
    // WifiManager logic without ever calling EspWiFiConnector's real,
    // wall-clock-bound WiFi.status() polling loops.
    WifiManager* wifiManager;

    volatile bool scanInProgress;
    volatile bool scanComplete;
    String scanResults;
    unsigned long lastScanTime;
    static constexpr unsigned long SCAN_CACHE_MS = 30000;

    bool startAPMode();
    bool startStationMode();
    bool processWiFiCredentials(AsyncWebServerRequest* request, const String& ssid,
                                const String& password);

    static void webServerTask(void* parameter);
    void createWebServerTask();
    void setupRoutes();

    void startAsyncScan();
    String scanWiFiNetworks();
    void onWiFiScanComplete(int networksFound);

    // Builds the current device state as JSON into outBuffer (size
    // outBufferSize). Returns the number of bytes written (0 on failure).
    // Pushed to a client on WS_EVT_CONNECT and broadcast to all clients
    // whenever MissionControl::consumeStateDirty() reports a change.
    size_t buildCurrentStateJson(char* outBuffer, size_t outBufferSize);

    // Polled every webServerTask tick: if MissionControl reports state
    // changed since the last check, broadcasts a fresh state JSON to every
    // connected WebSocket client.
    void broadcastStateIfDirty();

#ifdef UNIT_TEST
    // Test-only access so native integration tests can inject fake
    // IWiFiConnector/IPreferencesStore implementations and drive route
    // handlers directly without a real WiFi/NVS stack.
    friend class CommsTestAccess;
#endif
};