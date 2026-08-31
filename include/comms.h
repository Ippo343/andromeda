#pragma once
#include <ArduinoLog.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

#include "WiFi.h"
#include "comms-utils.h"
#include "device-identity.h"
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

    // Hot-switches an already-running station-mode device into the setup AP, without
    // restarting the web server (AsyncTCP listens on 0.0.0.0, so it keeps serving once the AP
    // interface is up - only the WiFi mode and the DNS captive-portal server need to change).
    // Called by the WiFi-recovery monitor (wifi-esp-adapters.cpp) once a disconnect has gone
    // unresolved past its dead time, so a router outage doesn't strand the device
    // unreachable forever. A no-op if already in AP mode.
    void enterAPFallbackMode();

   private:
    Comms();

    AsyncWebServer server;
    AsyncWebSocket ws;

    TaskHandle_t webServerTaskHandle;
    DNSServer* dnsServer;
    bool isAPMode;

    // The device name actually applied to the AP SSID / mDNS hostname at
    // startup - captured once in startAPMode()/startStationMode() so the
    // state broadcast can flag "rename pending reboot" the same way MODEL
    // does (see buildCurrentStateJson()), even if DeviceIdentity::
    // getDeviceName() has since changed via a live rename.
    String runningDeviceName;

    EspWiFiConnector wifiConnector;
    EspPreferencesStore preferencesStore;
    WifiManager realWifiManager;

    // Always points at realWifiManager in production. Native tests
    // (CommsTestAccess) repoint this at a WifiManager built from fake
    // IWiFiConnector/IPreferencesStore, so route handlers exercise real
    // WifiManager logic without ever calling EspWiFiConnector's real,
    // wall-clock-bound WiFi.status() polling loops.
    WifiManager* wifiManager;

    // Outcome of the most recent /save credential probe, driven off the
    // async_tcp task by saveWorkerTask() and polled by the still-connected
    // setup client via GET /save-status. Idle until the first /save; each
    // accepted /save resets it to Pending. Written from the worker task, read
    // from the async_tcp task - a plain scalar, single writer, so volatile is
    // enough (same treatment as scanInProgress/scanComplete).
    enum class SaveStatus
    {
        Idle,
        Pending,
        Connected,
        Failed
    };
    volatile SaveStatus saveStatus = SaveStatus::Idle;

    // Blocking WiFi-probe worker for the /save handler. Takes ownership of a
    // heap-allocated SaveJob (ssid/password copied off the request) and frees
    // it. Runs testAndPersistCredentials(); on success, reboots into station
    // mode after a short grace period so the client can still poll the
    // "connected" result. Never runs on the async_tcp task (#114).
    static void saveWorkerTask(void* param);

    volatile bool scanInProgress;
    volatile bool scanComplete;
    // Written from the WiFi event task (onWiFiScanComplete()), read from the async_tcp task
    // (scanWiFiNetworks()) - a plain String isn't safe to share across that without a lock (a
    // concurrent read during reassignment could see a freed/partial buffer), so every access
    // to scanResults itself goes through scanResultsMux.
    String scanResults;
    portMUX_TYPE scanResultsMux = portMUX_INITIALIZER_UNLOCKED;
    unsigned long lastScanTime;
    static constexpr unsigned long SCAN_CACHE_MS = 30000;

    // millis() of the last state broadcast that actually went out, and the
    // floor interval between broadcasts. A colour/brightness drag marks the
    // state dirty ~100x/sec (setLiveColor()/setMaxBrightness() run straight
    // from the WS task), and each broadcast is a full ~3 KB state JSON -
    // pushed that fast they overrun the per-client AsyncWebSocket send queue
    // and get dropped. See broadcastStateIfDirty().
    unsigned long lastBroadcastMs;
    static constexpr unsigned long MIN_BROADCAST_INTERVAL_MS = 100;

    // A change-triggered broadcast can be silently dropped for any one client whose send
    // queue is already full (AsyncWebSocket just drops it - see textAll()'s ignored return),
    // which is exactly the congested/weak-link case bad weather produces. Since broadcasts
    // otherwise only fire on change, that client would stay stale until some unrelated state
    // change happened to trigger the next one - possibly never. Force a full resync at this
    // interval regardless of the dirty flag; see broadcastStateIfDirty().
    static constexpr unsigned long STATE_KEEPALIVE_INTERVAL_MS = 5000;

    // Monotonic millisecond clock used by the broadcast throttle. The
    // indirection exists only so the native comms integration test can advance
    // time deterministically - the FastLED stub millis() it links against is
    // frozen at 0. nullptr (the only production value) means "use millis()".
    unsigned long (*nowMsFn)() = nullptr;
    inline unsigned long nowMs() const { return nowMsFn ? nowMsFn() : millis(); }

    // Shared radio/DNS setup for startAPMode() and enterAPFallbackMode() - see comms.cpp.
    void beginAPBroadcast();

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