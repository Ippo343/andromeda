#pragma once
#include <ArduinoLog.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <freertos/semphr.h>

#include "WiFi.h"
#include "comms-utils.h"
#include "device-identity.h"
#include "mission-control.h"
#include "secrets.h"
#include "utils.h"
#include "wifi-esp-adapters.h"
#include "wifi-manager.h"
#include "ws-metrics-builder.h"
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

    // What setup() actually did, so main.cpp can tell a genuine connection failure
    // (stored credentials exist but didn't work - something's actually wrong) apart from
    // a brand-new device's expected first-boot state (never configured at all - landing
    // in AP mode is normal there, not an error).
    enum class SetupOutcome
    {
        Connected,
        NeverConfigured,
        ConnectFailed
    };
    SetupOutcome setup();
    void printWifiStatus();

    // The mDNS names this device currently answers to, as a JSON array literal (e.g.
    // `["kitchen.local","l70-a1b2.local","andromeda.local"]`) - shared by GET /device-info and
    // the WiFi setup page. Each entry is the resolved winner of a device/model/andromeda name
    // pair (see include/mdns-hosts.h, issues #135/#210); a pair only shows its "-<uid>" half
    // once a real network connection has confirmed the plain half is already taken by someone
    // else. Safe to call before mDNS has started (AP mode's setup page needs this before any
    // radio work has happened, and before there's a network to check availability against at
    // all - it falls back to the plain, unchecked names then).
    String mdnsHostsJson();

    // Hot-switches an already-running station-mode device into the setup AP, without
    // restarting the web server (AsyncTCP listens on 0.0.0.0, so it keeps serving once the AP
    // interface is up - only the WiFi mode and the DNS captive-portal server need to change).
    // Called by the WiFi-recovery monitor (wifi-esp-adapters.cpp) once a disconnect has gone
    // unresolved past its dead time, so a router outage doesn't strand the device
    // unreachable forever. A no-op if already in AP mode.
    void enterAPFallbackMode();

    // Thread-safe snapshot of isAPMode, guarded the same way as the field itself (see
    // apStateMux). Used by the background AP-rejoin monitor (wifi-esp-adapters.cpp) to
    // decide whether a periodic stored-credentials retry is worth attempting.
    bool isInAPMode() const;

   private:
    Comms();

    AsyncWebServer server;
    AsyncWebSocket ws;

    TaskHandle_t webServerTaskHandle;

    // dnsServer/isAPMode/runningDeviceName are written from beginAPBroadcast()
    // (WifiRecovery monitor task or boot) and startStationMode() (boot only), and read
    // every ~10ms from webServerTask's captive-portal loop and from buildCurrentStateJson()
    // on the async_tcp task - both possibly on a different core. All three are only ever
    // written together, so one spinlock covers all three; runningDeviceName is a fixed
    // buffer rather than a String specifically so neither the write nor the read has to
    // allocate while holding it (the /scan critical section got this wrong for scanResults -
    // see the malloc-under-spinlock fix there - this doesn't repeat it).
    mutable portMUX_TYPE apStateMux = portMUX_INITIALIZER_UNLOCKED;
    DNSServer* dnsServer;
    bool isAPMode;

    // The device name actually applied to the AP SSID / mDNS hostname at
    // startup - captured once in startAPMode()/startStationMode() so the
    // state broadcast can flag "rename pending reboot" the same way MODEL
    // does (see buildCurrentStateJson()), even if DeviceIdentity::
    // getDeviceName() has since changed via a live rename.
    char runningDeviceName[DeviceUid::MAX_NAME_LENGTH + 1] = "";

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
    // millis() when the current async scan was kicked off. A scan that never
    // fires its SCAN_DONE event (WiFi.scanNetworks() returned an error
    // synchronously, or the event was lost during an AP<->STA mode change)
    // would otherwise leave scanInProgress stuck true and /scan answering
    // "scanning" for the rest of the session - scanWiFiNetworks() reaps a
    // scan older than SCAN_STALE_MS via comms-utils.h's scanIsStale().
    // volatile like scanInProgress/scanComplete right above: startAsyncScan()
    // writes this from either the async_tcp task (via scanWiFiNetworks()) or
    // the one-shot InitScan/APFallbackScan task (startAPMode()/
    // enterAPFallbackMode()), and scanWiFiNetworks()'s staleness check reads
    // it back on the async_tcp task - the same cross-task sharing this file's
    // own convention already marks volatile everywhere else.
    volatile unsigned long scanStartedAt = 0;
    static constexpr unsigned long SCAN_STALE_MS = 20000;
    // Written from the WiFi event task (onWiFiScanComplete()), read from the async_tcp task
    // (scanWiFiNetworks()) - a plain String isn't safe to share across that without a lock (a
    // concurrent read during reassignment could see a freed/partial buffer), so every access
    // to scanResults itself goes through scanResultsMutex. A real FreeRTOS mutex, not a
    // spinlock (contrast apStateMux above): the guarded operation is a String copy, which
    // allocates, and holding a spinlock (disables interrupts) across an allocation risks a
    // deadlock/abort against the heap allocator's own lock.
    String scanResults;
    SemaphoreHandle_t scanResultsMutex;
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

    // Per-client opt-in "metrics" WS push (#214) - deliberately separate from the "state"
    // broadcast above: that one goes to every client at up to 10Hz during a live drag, this one
    // is a much smaller, tiered payload only for clients that asked (the Advanced page). Fixed
    // array, zero allocation. clientId == 0 is the empty-slot sentinel: AsyncWebSocket's own
    // _cNextId starts at 1, so no real client is ever assigned id 0.
    struct MetricsSubscriber
    {
        uint32_t clientId = 0;
        // Set on subscribe (including a re-subscribe of an already-tracked client) and cleared
        // once pushMetricsIfDue() sends that client its next frame - the mechanism for "send
        // the one-off full frame immediately, then fall into the regular tiered cadence".
        bool needsFullFrame = false;
    };
    // Matches AsyncWebSocket's own DEFAULT_MAX_WS_CLIENTS default (8 on ESP32 - see
    // cleanupClients()'s call site in setupRoutes()), so a full house of connected clients can
    // all subscribe. Kept as our own literal rather than referencing the library's macro
    // directly: that macro is defined by <ESPAsyncWebServer.h> and has no equivalent in
    // test/mocks/ESPAsyncWebServer.h, so the native build (which links the mock, not the real
    // library) would fail to compile against it.
    static constexpr size_t MAX_METRICS_SUBSCRIBERS = 8;
    MetricsSubscriber metricsSubscribers[MAX_METRICS_SUBSCRIBERS];
    // Guards metricsSubscribers - held only across the fixed-array scan/mutate in
    // addMetricsSubscriber()/removeMetricsSubscriber()/pushMetricsIfDue()'s snapshot step, same
    // spinlock-over-a-POD-copy discipline as apStateMux (no allocation while held).
    mutable portMUX_TYPE metricsSubMux = portMUX_INITIALIZER_UNLOCKED;

    unsigned long lastMetricsPushMs = 0;
    unsigned long lastMetricsOtaTierMs = 0;
    static constexpr unsigned long METRICS_PUSH_INTERVAL_MS = 1000;
    static constexpr unsigned long METRICS_OTA_TIER_INTERVAL_MS = 10000;

    void addMetricsSubscriber(uint32_t clientId);
    void removeMetricsSubscriber(uint32_t clientId);
    // Called once per webServerTask tick (like broadcastStateIfDirty()). Sends a full frame to
    // any subscriber with needsFullFrame set, and a volatile(+OTA, when due) frame to the rest
    // once METRICS_PUSH_INTERVAL_MS has elapsed. No-op, before any sensor read, when there are
    // no subscribers.
    void pushMetricsIfDue();
    // Gathers the live values (PerformanceMonitor/PowerMonitor/OtaUpdater/etc) into `out`.
    // Shared by pushMetricsIfDue() and the /metrics HTTP route so the two payloads can't drift
    // apart. `includeStatic`/`includeOta` on `out` select which tiers get filled in.
    void fillMetricsSnapshot(WsMetricsBuilder::MetricsSnapshot& out, bool includeStatic,
                             bool includeOta);

    // Monotonic millisecond clock used by the broadcast throttle. The
    // indirection exists only so the native comms integration test can advance
    // time deterministically - the FastLED stub millis() it links against is
    // frozen at 0. nullptr (the only production value) means "use millis()".
    unsigned long (*nowMsFn)() = nullptr;
    inline unsigned long nowMs() const { return nowMsFn ? nowMsFn() : millis(); }

    // Shared radio/DNS setup for startAPMode() and enterAPFallbackMode() - see comms.cpp.
    void beginAPBroadcast();

    // Starts the mDNS responder (once per boot - see mdnsStarted) and (re)registers the
    // delegated fallback hostnames (include/mdns-hosts.h) against `ip`. Called from both
    // startStationMode() and beginAPBroadcast(), and again on every DHCP renewal, since the
    // delegated address list is static unlike the primary hostname's - see comms.cpp.
    void startMdns(IPAddress ip);
    // True once MDNS.begin() has succeeded this boot - startMdns() is idempotent on the
    // "start the responder" half (beginAPBroadcast() can re-enter it at runtime via the
    // WiFi-recovery monitor while station mode's MDNS.begin() already ran) but always
    // re-registers the delegated addresses, since those must track the current IP.
    bool mdnsStarted = false;

    // The three resolved name-pair winners (device/model/andromeda - see include/mdns-hosts.h),
    // decided once by startMdns()'s first call and reused by every later call (IP renewal) and
    // by mdnsHostsJson()/printWifiStatus() - availability can only be probed once there's a real
    // network to ask (WL_CONNECTED), so an AP-mode-only boot never resolves anything beyond the
    // plain, unchecked defaults. A device that boots to AP mode always reboots before it can
    // join a real network (a successful /save credential test ends in ESP.restart(), same as a
    // rename - see setDeviceName()'s comment), so there's no live AP-to-station transition within
    // one boot that would need re-resolving mid-flight.
    String resolvedDeviceHost;
    String resolvedModelHost;
    String resolvedAndromedaHost;

    // The exact hostname registered with MDNS.begin() at boot - captured once, alongside the
    // resolved* members above, and reused for every later startMdns() call (IP renewal). Must
    // never be recomputed live from DeviceIdentity::getMdnsHostname(): a rename only takes
    // effect after reboot (see DeviceIdentity's header comment), so a live rename would make a
    // fresh call return a name this device hasn't actually registered as its primary yet - and
    // registerMdnsDelegates() would then mistake the still-real primary for an unclaimed name
    // and push it through the delegate-remove/re-add path, which is meant for delegated
    // hostnames only.
    String primaryMdnsHostname;

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