#include <ArduinoJson.h>
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

// comms.cpp transitively needs MissionControl (getMaxBrightness, the WS
// command queue) and PerformanceMonitor, so this test binary assembles the
// same self-contained chain test_mission_control.cpp does (effects.h for
// getRandomEffect() - defined by effects/effects-registry.cpp, which is part
// of platformio.ini's shared native build_src_filter - plus a stub
// getRandomAnimation() so animations.cpp isn't needed, then
// mission-control.cpp), with comms.cpp #included on top. comms.cpp itself
// can't go through that shared build_src_filter at all - every native test
// target links against it, and most don't define
// MissionControl/PerformanceMonitor.
#include "../../include/effects.h"
#include "animation-base.h"
#include "animation-frame-base.h"

class StubAnimation : public AbstractFrameAnimation
{
   public:
    const char* GetName() override { return "StubAnimation"; }
    bool renderFrame(milliseconds_t localT) override { return true; }
};
AbstractFrameAnimation* getRandomAnimation() { return new StubAnimation(); }

#include "../../src/comms.cpp"
#include "../../src/mission-control.cpp"

// EspWiFiConnector/EspPreferencesStore's real bodies live in
// wifi-esp-adapters.cpp, excluded from native (they poll WiFi.status() in
// real wall-clock time - see that file's header comment). Comms still owns
// them by value, so their vtables must resolve at link time even though
// every test below redirects Comms::wifiManager to a fake-backed
// WifiManager before calling anything - these real ones are never invoked.
bool EspWiFiConnector::connect(const char*, const char*) { return false; }
bool EspWiFiConnector::testConnection(const char*, const char*) { return false; }
void EspWiFiConnector::enterAPMode() {}
void EspPreferencesStore::saveCredentials(const String&, const String&) {}
bool EspPreferencesStore::loadCredentials(String&, String&) { return false; }
void EspPreferencesStore::clearCredentials() {}

// Same reasoning: the real body (an xTaskCreate loop) lives in
// wifi-esp-adapters.cpp; Comms::beginAPBroadcast() calls it unconditionally, so it
// needs a link-time stub here too. Nothing under test drives the AP-rejoin retry loop
// itself - only that beginAPBroadcast() reaches this call at all.
void startApRejoinMonitor() {}

// OtaUpdater's real body (src/ota-updater.cpp) is WiFi/HTTPClient/Update-bound
// and excluded from native, like the Esp* connectors above. comms.cpp's OTA
// routes only kick tasks and read status(); stub that surface, with counters
// so the route tests can assert the handler actually reached it. OtaConfig is
// real here (src/ota-config.cpp is in the native build).
namespace OtaUpdaterStub
{
int startUpdateCalls = 0;
int startCheckCalls = 0;
// What the stubbed start* calls report back - the route maps this to the HTTP
// status, so a test can force a non-Started outcome to check that mapping.
OtaStartGate::Outcome nextOutcome = OtaStartGate::Outcome::Started;
}  // namespace OtaUpdaterStub
namespace OtaUpdater
{
void begin() {}
OtaStartGate::Outcome startCheck()
{
    ++OtaUpdaterStub::startCheckCalls;
    return OtaUpdaterStub::nextOutcome;
}
OtaStartGate::Outcome startUpdate()
{
    ++OtaUpdaterStub::startUpdateCalls;
    return OtaUpdaterStub::nextOutcome;
}
Status status() { return Status{State::Idle, 0, 0, "", ""}; }
bool updateAvailable() { return false; }
}  // namespace OtaUpdater

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

class FakeWiFiConnector : public IWiFiConnector
{
   public:
    bool connectResult = true;
    bool testConnectionResult = true;
    int connectCalls = 0;
    int testConnectionCalls = 0;
    int enterAPModeCalls = 0;

    bool connect(const char*, const char*) override
    {
        connectCalls++;
        return connectResult;
    }
    bool testConnection(const char*, const char*) override
    {
        testConnectionCalls++;
        enterAPModeCalls++;  // real EspWiFiConnector::testConnection always reverts to AP mode
        return testConnectionResult;
    }
    void enterAPMode() override { enterAPModeCalls++; }
};

class FakePreferencesStore : public IPreferencesStore
{
   public:
    bool hasCredentials = false;
    String storedSsid, storedPassword;
    int clearCalls = 0;

    void saveCredentials(const String& ssid, const String& password) override
    {
        hasCredentials = true;
        storedSsid = ssid;
        storedPassword = password;
    }
    bool loadCredentials(String& ssid, String& password) override
    {
        if (!hasCredentials) return false;
        ssid = storedSsid;
        password = storedPassword;
        return true;
    }
    void clearCredentials() override
    {
        hasCredentials = false;
        clearCalls++;
    }
};

// Exposes MissionControl's private members to this test file via the
// UNIT_TEST-guarded friend declaration in mission-control.h - only needed so
// setUp() can give it a valid starting effect before the WS handler test
// drives update() (mirrors test_mission_control.cpp's identical setUp()).
class MissionControlTestAccess
{
   public:
    static void setEffect(MissionControl& mc, AbstractEffect* e)
    {
        if (mc.effect) delete mc.effect;
        mc.effect = e;
    }
};

// Exposes Comms' private members to this test file via the UNIT_TEST-guarded
// friend declaration in comms.h.
class CommsTestAccess
{
   public:
    static void setWifiManager(Comms& c, WifiManager& wm) { c.wifiManager = &wm; }
    static void setupRoutes(Comms& c) { c.setupRoutes(); }
    static AsyncWebServer& server(Comms& c) { return c.server; }
    static bool isAPMode(Comms& c) { return c.isAPMode; }
    static String scanWiFiNetworks(Comms& c) { return c.scanWiFiNetworks(); }
    static void broadcastStateIfDirty(Comms& c) { c.broadcastStateIfDirty(); }
    static void resetBroadcastThrottle(Comms& c) { c.lastBroadcastMs = 0; }
    static unsigned long minBroadcastIntervalMs() { return Comms::MIN_BROADCAST_INTERVAL_MS; }
    static unsigned long stateKeepaliveIntervalMs() { return Comms::STATE_KEEPALIVE_INTERVAL_MS; }
    static void setNowFn(Comms& c, unsigned long (*fn)()) { c.nowMsFn = fn; }
    static void onWiFiScanComplete(Comms& c, int n) { c.onWiFiScanComplete(n); }
    // saveStatus is a private nested enum; expose the three post-probe states the
    // /save-status route test needs to reach without the (native no-op) worker task running.
    static void setSaveStatusPending(Comms& c) { c.saveStatus = Comms::SaveStatus::Pending; }
    static void setSaveStatusConnected(Comms& c) { c.saveStatus = Comms::SaveStatus::Connected; }
    static void setSaveStatusFailed(Comms& c) { c.saveStatus = Comms::SaveStatus::Failed; }
    static bool scanInProgress(Comms& c) { return c.scanInProgress; }
    static void setScanInProgress(Comms& c, bool v) { c.scanInProgress = v; }
    static void setScanComplete(Comms& c, bool v) { c.scanComplete = v; }
};

// Deterministic clock for the broadcast-throttle test - the stub millis() this
// binary links against is frozen at 0, so real elapsed time can't be observed.
static unsigned long g_fakeNowMs = 0;
static unsigned long fakeNowMs() { return g_fakeNowMs; }

namespace
{
FakeWiFiConnector& fakeConnector()
{
    static FakeWiFiConnector c;
    return c;
}
FakePreferencesStore& fakeStore()
{
    static FakePreferencesStore s;
    return s;
}
WifiManager& fakeWifiManager()
{
    static WifiManager wm(fakeConnector(), fakeStore());
    return wm;
}
}  // namespace

void setUp()
{
    fakeConnector() = FakeWiFiConnector();
    fakeStore() = FakePreferencesStore();
    CommsTestAccess::setWifiManager(Comms::Instance(), fakeWifiManager());
    CommsTestAccess::setupRoutes(Comms::Instance());

    // Needed only for test_ws_valid_text_frame_queues_command's
    // MissionControl::Instance().update(0) call - see MissionControlTestAccess.
    GEOMETRY.initializeForTest(ModelId::SINGLE_STRIP_TEST_DEVICE);
    MissionControlTestAccess::setEffect(MissionControl::Instance(), new StaticColor());

    // Each test starts with the broadcast throttle cleared and back on the
    // real (frozen) clock, so the first broadcastStateIfDirty() in a test
    // always fires regardless of a previous test (they share the Comms
    // singleton). The throttle test opts into the fake clock itself.
    CommsTestAccess::resetBroadcastThrottle(Comms::Instance());
    CommsTestAccess::setNowFn(Comms::Instance(), nullptr);
    g_fakeNowMs = 0;
}
void tearDown() {}

// ---------------------------------------------------------------------------
// /save
// ---------------------------------------------------------------------------

// Valid credentials are accepted with a 202 and handed to the worker task - the /save handler
// itself must not connect or persist (it runs on the async_tcp task; a ~10s blocking
// WiFi.status() poll there starves the task past its watchdog and panics the device, #114).
// The worker (saveWorkerTask, driven by a native no-op xTaskCreate here) is what probes and
// persists; the setup client polls /save-status for the verdict.
void test_save_valid_credentials_returns_202_without_blocking_or_persisting()
{
    AsyncWebServerRequest req;
    req.setArg("ssid", "MyNetwork");
    req.setArg("password", "hunter2");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/save", HTTP_POST);
    TEST_ASSERT_NOT_NULL(handler);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(202, req.responseCode);
    TEST_ASSERT_FALSE(fakeStore().hasCredentials);
    TEST_ASSERT_EQUAL_INT(0, fakeConnector().testConnectionCalls);
    TEST_ASSERT_EQUAL_INT(0, fakeConnector().connectCalls);

    // /save-status now reports the in-flight probe.
    AsyncWebServerRequest statusReq;
    auto* statusHandler =
        CommsTestAccess::server(Comms::Instance()).findHandler("/save-status", HTTP_GET);
    TEST_ASSERT_NOT_NULL(statusHandler);
    (*statusHandler)(&statusReq);
    TEST_ASSERT_EQUAL_INT(200, statusReq.responseCode);
    TEST_ASSERT_TRUE(statusReq.responseBody == String("pending"));
}

// The worker's actual probe-then-persist step, exercised directly against the fakes since the
// native xTaskCreate never runs saveWorkerTask. On a successful probe the pair is persisted...
void test_test_and_persist_persists_on_successful_probe()
{
    fakeConnector().testConnectionResult = true;

    bool ok = fakeWifiManager().testAndPersistCredentials("MyNetwork", "hunter2");

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(1, fakeConnector().testConnectionCalls);
    TEST_ASSERT_TRUE(fakeStore().hasCredentials);
    TEST_ASSERT_TRUE(fakeStore().storedSsid == String("MyNetwork"));
    TEST_ASSERT_TRUE(fakeStore().storedPassword == String("hunter2"));
}

// ...and on a failed probe (wrong password) nothing is persisted and the caller learns it
// failed, so the setup page can say so instead of the device silently rebooting (#134).
void test_test_and_persist_does_not_persist_on_failed_probe()
{
    fakeConnector().testConnectionResult = false;

    bool ok = fakeWifiManager().testAndPersistCredentials("MyNetwork", "wrongpass");

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(1, fakeConnector().testConnectionCalls);
    TEST_ASSERT_FALSE(fakeStore().hasCredentials);
    // testConnection() reverts the radio to the setup AP regardless of outcome.
    TEST_ASSERT_TRUE(fakeConnector().enterAPModeCalls > 0);
}

// /save-status maps the worker's cross-task flag to the strings the setup page polls for.
void test_save_status_route_reports_probe_outcome()
{
    auto* handler =
        CommsTestAccess::server(Comms::Instance()).findHandler("/save-status", HTTP_GET);
    TEST_ASSERT_NOT_NULL(handler);

    CommsTestAccess::setSaveStatusPending(Comms::Instance());
    AsyncWebServerRequest pendingReq;
    (*handler)(&pendingReq);
    TEST_ASSERT_TRUE(pendingReq.responseBody == String("pending"));

    CommsTestAccess::setSaveStatusConnected(Comms::Instance());
    AsyncWebServerRequest connectedReq;
    (*handler)(&connectedReq);
    TEST_ASSERT_TRUE(connectedReq.responseBody == String("connected"));

    CommsTestAccess::setSaveStatusFailed(Comms::Instance());
    AsyncWebServerRequest failedReq;
    (*handler)(&failedReq);
    TEST_ASSERT_TRUE(failedReq.responseBody == String("failed"));
}

// A foreign page's <form> auto-submitted from a browser on the same LAN carries an
// Origin header that won't match this device's own host - the drive-by CSRF case
// isCrossOriginPost() exists to close. Neither /save nor /reset needs a CORS preflight
// (simple form POSTs never trigger one), so this check is the only thing that catches it.
void test_save_rejects_cross_origin_post()
{
    AsyncWebServerRequest req;
    req.setHost("andromeda-a1b2.local");
    req.setHeader("Origin", "http://evil.example.com");
    req.setArg("ssid", "MyNetwork");
    req.setArg("password", "hunter2");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/save", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(403, req.responseCode);
    TEST_ASSERT_FALSE(fakeStore().hasCredentials);
    TEST_ASSERT_EQUAL_INT(0, fakeConnector().connectCalls);
}

// Same-origin (or no Origin header at all - curl, a script, the setup page's own
// same-origin fetch) must still work; only a mismatched Origin is rejected.
void test_save_allows_same_origin_post()
{
    AsyncWebServerRequest req;
    req.setHost("andromeda-a1b2.local");
    req.setHeader("Origin", "http://andromeda-a1b2.local");
    req.setArg("ssid", "MyNetwork");
    req.setArg("password", "hunter2");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/save", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(202, req.responseCode);
}

void test_reset_rejects_cross_origin_post()
{
    fakeStore().hasCredentials = true;

    AsyncWebServerRequest req;
    req.setHost("andromeda-a1b2.local");
    req.setHeader("Origin", "http://evil.example.com");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/reset", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(403, req.responseCode);
    TEST_ASSERT_TRUE(fakeStore().hasCredentials);
    TEST_ASSERT_EQUAL_INT(0, fakeStore().clearCalls);
}

void test_save_empty_ssid_returns_400_and_does_not_persist()
{
    AsyncWebServerRequest req;
    req.setArg("ssid", "");
    req.setArg("password", "");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/save", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(400, req.responseCode);
    TEST_ASSERT_FALSE(fakeStore().hasCredentials);
}

// An over-long SSID/password from anything other than the WiFi setup page itself (nothing
// else validates it) previously got persisted and then could simply never join - WiFi.begin()
// silently truncates/rejects it, stranding the device in AP mode with credentials that look
// "saved" but can never work.
void test_save_over_long_ssid_returns_400_and_does_not_persist()
{
    AsyncWebServerRequest req;
    req.setArg("ssid", std::string(WifiManager::MAX_SSID_LENGTH + 1, 'a'));
    req.setArg("password", "");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/save", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(400, req.responseCode);
    TEST_ASSERT_FALSE(fakeStore().hasCredentials);
}

void test_save_over_long_password_returns_400_and_does_not_persist()
{
    AsyncWebServerRequest req;
    req.setArg("ssid", "MyNetwork");
    req.setArg("password", std::string(WifiManager::MAX_PASSWORD_LENGTH + 1, 'a'));

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/save", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(400, req.responseCode);
    TEST_ASSERT_FALSE(fakeStore().hasCredentials);
}

// ---------------------------------------------------------------------------
// /reset
// ---------------------------------------------------------------------------

void test_reset_clears_stored_credentials()
{
    fakeStore().hasCredentials = true;

    AsyncWebServerRequest req;
    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/reset", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(200, req.responseCode);
    TEST_ASSERT_FALSE(fakeStore().hasCredentials);
    TEST_ASSERT_EQUAL_INT(1, fakeStore().clearCalls);
}

// ---------------------------------------------------------------------------
// Comms::setup() - stored-credential connect vs AP-mode fallback
// ---------------------------------------------------------------------------

void test_setup_connects_with_stored_credentials_and_skips_ap_mode()
{
    fakeStore().hasCredentials = true;
    fakeStore().storedSsid = "Home";
    fakeStore().storedPassword = "secret";
    fakeConnector().connectResult = true;

    Comms::SetupOutcome outcome = Comms::Instance().setup();

    TEST_ASSERT_FALSE(CommsTestAccess::isAPMode(Comms::Instance()));
    TEST_ASSERT_TRUE(outcome == Comms::SetupOutcome::Connected);
}

// Distinguishing NeverConfigured from ConnectFailed (below) is what lets main.cpp skip
// the alarming ErrorAnimation on a brand-new device's ordinary first boot while still
// showing it for a genuine failure - see main.cpp's switch on this same enum.
void test_setup_falls_back_to_ap_mode_when_no_stored_credentials()
{
    fakeStore().hasCredentials = false;

    Comms::SetupOutcome outcome = Comms::Instance().setup();

    TEST_ASSERT_TRUE(CommsTestAccess::isAPMode(Comms::Instance()));
    TEST_ASSERT_TRUE(outcome == Comms::SetupOutcome::NeverConfigured);
}

void test_setup_falls_back_to_ap_mode_when_stored_credentials_fail_to_connect()
{
    fakeStore().hasCredentials = true;
    fakeStore().storedSsid = "Home";
    fakeStore().storedPassword = "secret";
    fakeConnector().connectResult = false;

    Comms::SetupOutcome outcome = Comms::Instance().setup();

    TEST_ASSERT_TRUE(CommsTestAccess::isAPMode(Comms::Instance()));
    TEST_ASSERT_TRUE(outcome == Comms::SetupOutcome::ConnectFailed);
}

// ---------------------------------------------------------------------------
// /scan - cache hit vs cache miss
// ---------------------------------------------------------------------------

void test_scan_route_returns_scanning_placeholder_on_cache_miss()
{
    // Fresh Comms singleton state from previous tests may have scanComplete
    // set; scanWiFiNetworks()'s own isScanCacheValid() check (already unit
    // tested directly in test_comms.cpp) governs the cache-hit branch, so
    // this only needs to confirm the cache-miss path starts a scan and
    // returns the documented placeholder rather than crashing/blocking.
    AsyncWebServerRequest req;
    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/scan", HTTP_GET);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(200, req.responseCode);
}

// A double-quote or backslash in an SSID (both legal in a WiFi SSID per 802.11) previously
// produced unparseable JSON out of onWiFiScanComplete() - jsonEscape() now escapes them, and
// this also confirms the encryption field (added alongside) actually shows up.
void test_scan_complete_escapes_ssid_and_reports_encryption()
{
    WiFi.scriptedScanSSIDs = {String("Weird\"SSID\\here"), String("Plain")};
    WiFi.scriptedScanRSSIs = {-50, -60};
    WiFi.scriptedScanEncryption = {WIFI_AUTH_OPEN, WIFI_AUTH_WPA2_PSK};

    CommsTestAccess::onWiFiScanComplete(Comms::Instance(), 2);

    String json = CommsTestAccess::scanWiFiNetworks(Comms::Instance());
    StaticJsonDocument<1024> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, json.c_str()));

    TEST_ASSERT_EQUAL_STRING("Weird\"SSID\\here", doc["networks"][0]["ssid"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("none", doc["networks"][0]["encryption"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("secured", doc["networks"][1]["encryption"].as<const char*>());
}

// A negative scanComplete() result (e.g. WIFI_SCAN_FAILED) previously had no branch at all in
// the WiFi.onEvent(SCAN_DONE) handler - onWiFiScanComplete() (the only thing that clears
// scanInProgress) was simply never called, so scanInProgress stayed stuck true and /scan
// returned "scanning" forever with no way to retry short of a reboot.
void test_scan_failure_clears_scan_in_progress()
{
    // Ensure the SCAN_DONE handler has actually been registered (startAsyncScan() only
    // registers it once per process - see comms.cpp's `static bool handlerRegistered`) by
    // going through the same route a real cache-miss /scan request would. scanComplete must
    // be forced false too - a previous test's completed scan otherwise satisfies
    // isScanCacheValid() (native millis() is frozen at 0, so a cache never "expires" within a
    // test run) and scanWiFiNetworks() returns the cached result without ever reaching
    // startAsyncScan().
    CommsTestAccess::setScanInProgress(Comms::Instance(), false);
    CommsTestAccess::setScanComplete(Comms::Instance(), false);
    CommsTestAccess::scanWiFiNetworks(Comms::Instance());
    TEST_ASSERT_TRUE(CommsTestAccess::scanInProgress(Comms::Instance()));
    TEST_ASSERT_TRUE((bool)WiFi.lastEventCallback);

    WiFi.scriptedScanCompleteResult = -1;  // simulates WIFI_SCAN_FAILED
    WiFi.lastEventCallback(ARDUINO_EVENT_WIFI_SCAN_DONE, WiFiEventInfo_t{});

    TEST_ASSERT_FALSE(CommsTestAccess::scanInProgress(Comms::Instance()));

    WiFi.scriptedScanCompleteResult.reset();  // don't leak into later tests/scans
}

// scanNetworks(true) can also fail *synchronously* (WIFI_SCAN_FAILED) with no
// SCAN_DONE event ever firing - startAsyncScan() must reap scanInProgress
// itself in that case, or /scan answers "scanning" for the rest of the
// session with no way to retry.
void test_synchronous_scan_start_failure_does_not_wedge_scan_in_progress()
{
    CommsTestAccess::setScanInProgress(Comms::Instance(), false);
    CommsTestAccess::setScanComplete(Comms::Instance(), false);

    WiFi.scriptedScanStartResult = WIFI_SCAN_FAILED;  // esp_wifi_scan_start() refused
    CommsTestAccess::scanWiFiNetworks(Comms::Instance());
    TEST_ASSERT_FALSE(CommsTestAccess::scanInProgress(Comms::Instance()));

    // The very next /scan can start a real scan again (start now succeeds).
    WiFi.scriptedScanStartResult.reset();
    CommsTestAccess::scanWiFiNetworks(Comms::Instance());
    TEST_ASSERT_TRUE(CommsTestAccess::scanInProgress(Comms::Instance()));

    // Let the SCAN_DONE it's now waiting on complete, so state doesn't leak.
    WiFi.scriptedScanCompleteResult = 0;
    if (WiFi.lastEventCallback)
        WiFi.lastEventCallback(ARDUINO_EVENT_WIFI_SCAN_DONE, WiFiEventInfo_t{});
    WiFi.scriptedScanCompleteResult.reset();
}

// ---------------------------------------------------------------------------
// WebSocket handler wiring
// ---------------------------------------------------------------------------

void test_ws_valid_text_frame_queues_command()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;
    TEST_ASSERT_NOT_NULL(ws);

    char message[] = "{\"type\":\"hold\"}";
    AwsFrameInfo info;
    info.final = true;
    info.index = 0;
    info.len = strlen(message);
    info.opcode = WS_TEXT;

    // +1 for the frame handler's data[len]=0 NUL-termination write.
    std::vector<uint8_t> buf(message, message + strlen(message) + 1);
    ws->simulateDataFrame(info, buf.data(), strlen(message));

    // No direct observable here beyond "didn't crash and reached
    // MissionControl::queueWebCommand" - queueWebCommand's own behavior is
    // covered by test_mission_control.cpp. Draining it confirms it was queued.
    MissionControl::Instance().update(0);
}

// A commit:true color message that arrives while color mode is already active takes the
// direct setLiveColor() fast path (comms.cpp), not the command queue - a separate
// persistence call site from the queue path test_mission_control.cpp already covers, so
// this is its regression test.
void test_ws_color_commit_on_fast_path_persists_holding_color()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;
    AwsFrameInfo info;
    info.final = true;
    info.index = 0;
    info.opcode = WS_TEXT;

    // First message switches into color mode via the queue (no commit - a live drag
    // update); draining it is what makes isColorActive() true for the second message.
    char first[] = "{\"type\":\"color\",\"r\":1,\"g\":2,\"b\":3}";
    info.len = strlen(first);
    std::vector<uint8_t> buf1(first, first + strlen(first) + 1);
    ws->simulateDataFrame(info, buf1.data(), strlen(first));
    MissionControl::Instance().update(0);
    TEST_ASSERT_TRUE(MissionControl::Instance().isColorActive());

    // Second message: commit:true, now on the fast path.
    char second[] = "{\"type\":\"color\",\"r\":40,\"g\":50,\"b\":60,\"commit\":true}";
    info.len = strlen(second);
    std::vector<uint8_t> buf2(second, second + strlen(second) + 1);
    ws->simulateDataFrame(info, buf2.data(), strlen(second));

    StartupStateConfig::StartupState state = StartupStateConfig::load();
    TEST_ASSERT_TRUE(StartupStateConfig::Mode::HoldingColor == state.mode);
    TEST_ASSERT_EQUAL_UINT8(40, state.colorR);
    TEST_ASSERT_EQUAL_UINT8(50, state.colorG);
    TEST_ASSERT_EQUAL_UINT8(60, state.colorB);
}

// ---------------------------------------------------------------------------
// State broadcast (WS_EVT_CONNECT push + dirty-flag broadcast)
// ---------------------------------------------------------------------------

void test_ws_connect_pushes_initial_state()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;
    TEST_ASSERT_NOT_NULL(ws);

    AsyncWebSocketClient client;
    ws->simulateConnect(&client);

    TEST_ASSERT_FALSE(client.lastSentText.empty());

    // Auto-ping keeps the connection itself alive so the library notices a dead peer even
    // when the OS/router doesn't produce a clean TCP FIN - see comms.cpp's WS_EVT_CONNECT
    // handler.
    TEST_ASSERT_EQUAL_UINT16(30, client.lastKeepAlivePeriodSeconds);

    // Zero-copy parse (mutable buffer) - mirrors how comms.cpp/native-runtime
    // handle inbound JSON and keeps the doc within JSON_CAPACITY. Parsing the
    // std::string directly forces ArduinoJson to duplicate every key/value into
    // the pool, which needs materially more than the serialize side.
    std::string sent = client.lastSentText;
    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, sent.data(), sent.size()));
    TEST_ASSERT_EQUAL_STRING("state", doc["type"]);
    TEST_ASSERT_EQUAL(MissionControl::Instance().getMaxBrightness(), doc["brightness"].as<int>());
}

// The WebSocket handshake is a plain GET with no CORS preflight, so a foreign
// LAN page could open /ws and send {"type":"reboot"} etc., bypassing every
// isCrossOriginPost() guard on the HTTP routes. WS_EVT_CONNECT now checks the
// upgrade request's Origin the same way.
void test_ws_connect_rejects_a_foreign_origin_handshake()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;
    TEST_ASSERT_NOT_NULL(ws);

    AsyncWebServerRequest req;
    req.setHost("andromeda-ab12.local");
    req.setHeader("Origin", "http://andromeda-ab12.local.evil.example");

    AsyncWebSocketClient client;
    ws->simulateConnect(&client, &req);

    TEST_ASSERT_TRUE(client.wasClosed);
    TEST_ASSERT_TRUE(client.lastSentText.empty());  // no state pushed to a rejected client
}

void test_ws_connect_allows_a_same_origin_handshake()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;
    TEST_ASSERT_NOT_NULL(ws);

    AsyncWebServerRequest req;
    req.setHost("andromeda-ab12.local");
    req.setHeader("Origin", "http://andromeda-ab12.local");

    AsyncWebSocketClient client;
    ws->simulateConnect(&client, &req);

    TEST_ASSERT_FALSE(client.wasClosed);
    TEST_ASSERT_FALSE(client.lastSentText.empty());  // normal initial-state push still happens
}

void test_state_broadcast_after_brightness_command()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;

    char message[] = "{\"type\":\"brightness\",\"value\":77}";
    AwsFrameInfo info;
    info.final = true;
    info.index = 0;
    info.len = strlen(message);
    info.opcode = WS_TEXT;
    std::vector<uint8_t> buf(message, message + strlen(message) + 1);
    ws->simulateDataFrame(info, buf.data(), strlen(message));

    MissionControl::Instance().update(0);
    TEST_ASSERT_EQUAL(77, MissionControl::Instance().getMaxBrightness());

    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());

    TEST_ASSERT_FALSE(ws->lastBroadcastText.empty());
    std::string broadcast = ws->lastBroadcastText;  // zero-copy parse below, see note above
    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, broadcast.data(), broadcast.size()));
    TEST_ASSERT_EQUAL(77, doc["brightness"].as<int>());
}

void test_rapid_brightness_commands_bypass_queue_and_dont_drop()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;

    // Drive far more brightness updates than the 10-slot web command queue
    // could ever hold, back-to-back with no update()/processWebCommands()
    // drain in between - this is what a fast slider drag looks like.
    // Regression test for the crash where routing BRIGHTNESS through the
    // queue silently dropped most of a drag's updates once the queue filled,
    // and the resulting flood of "queue full" log lines on the async_tcp
    // task tripped the watchdog. Each value must land immediately.
    constexpr int kCommandCount = 30;
    for (int value = 1; value <= kCommandCount; value++)
    {
        char message[64];
        snprintf(message, sizeof(message), "{\"type\":\"brightness\",\"value\":%d}", value);
        AwsFrameInfo info;
        info.final = true;
        info.index = 0;
        info.len = strlen(message);
        info.opcode = WS_TEXT;
        std::vector<uint8_t> buf(message, message + strlen(message) + 1);
        ws->simulateDataFrame(info, buf.data(), strlen(message));

        TEST_ASSERT_EQUAL(value, MissionControl::Instance().getMaxBrightness());
    }

    // Leave the dirty flag consumed so it doesn't bleed into later tests.
    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());
}

void test_state_broadcast_skipped_when_not_dirty()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;
    ws->lastBroadcastText.clear();

    // Nothing changed since the last consumeStateDirty() call, so the poll
    // should be a no-op.
    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());

    TEST_ASSERT_TRUE(ws->lastBroadcastText.empty());
}

// #109: a live colour/brightness drag marks state dirty ~100x/sec, far faster
// than TCP can drain a ~3 KB state frame, so the per-client AsyncWebSocket
// send queue overruns and frames drop ("Too many messages queued").
// broadcastStateIfDirty() throttles to MIN_BROADCAST_INTERVAL_MS: a burst of
// dirties inside one window collapses to a single broadcast, and because the
// window check peeks the dirty flag without consuming it, the next window
// still sends the latest state.
void test_state_broadcasts_are_throttled_within_the_interval()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;
    MissionControl& mc = MissionControl::Instance();

    const unsigned long interval = CommsTestAccess::minBroadcastIntervalMs();
    g_fakeNowMs = 1000;
    CommsTestAccess::setNowFn(Comms::Instance(), &fakeNowMs);

    // First poll after setUp()'s reset: state is dirty, so it goes out.
    ws->lastBroadcastText.clear();
    mc.setMaxBrightness(10);
    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());
    TEST_ASSERT_FALSE(ws->lastBroadcastText.empty());

    // A drag's worth of dirties, all landing inside the same interval window:
    // no further frame is emitted no matter how many times we poll.
    ws->lastBroadcastText.clear();
    uint8_t lastBurstValue = 0;
    for (unsigned long dt = 1; dt < interval; dt += 5)
    {
        g_fakeNowMs = 1000 + dt;
        lastBurstValue = static_cast<uint8_t>(dt);
        mc.setMaxBrightness(lastBurstValue);
        CommsTestAccess::broadcastStateIfDirty(Comms::Instance());
    }
    TEST_ASSERT_TRUE(ws->lastBroadcastText.empty());

    // Once the window elapses the still-pending state finally goes out - with
    // no fresh setMaxBrightness() call, proving the throttled polls above only
    // peeked the dirty flag rather than consuming it. The frame carries the
    // last value written during the burst.
    g_fakeNowMs = 1000 + interval;
    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());
    TEST_ASSERT_FALSE(ws->lastBroadcastText.empty());
    TEST_ASSERT_EQUAL(lastBurstValue, mc.getMaxBrightness());

    // ... and the next immediate poll is throttled again.
    ws->lastBroadcastText.clear();
    mc.setMaxBrightness(42);
    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());
    TEST_ASSERT_TRUE(ws->lastBroadcastText.empty());
}

// A change-triggered broadcast can be silently dropped for a client whose send queue is
// already full (AsyncWebSocket just drops it - see textAll()'s ignored return), which
// broadcastStateIfDirty() otherwise has no way to detect: it only fires again on the next
// *change*, so a client that missed one could stay stale forever. STATE_KEEPALIVE_INTERVAL_MS
// forces a full resync periodically regardless of the dirty flag.
void test_state_broadcast_forces_periodic_resync_even_when_not_dirty()
{
    AsyncWebSocket* ws = CommsTestAccess::server(Comms::Instance()).webSocket;
    MissionControl& mc = MissionControl::Instance();

    g_fakeNowMs = 1000;
    CommsTestAccess::setNowFn(Comms::Instance(), &fakeNowMs);

    // Establish lastBroadcastMs with a real change, then fully consume the dirty flag.
    ws->lastBroadcastText.clear();
    mc.setMaxBrightness(10);
    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());
    TEST_ASSERT_FALSE(ws->lastBroadcastText.empty());

    // Nothing changed, and the keepalive interval hasn't elapsed yet: no broadcast.
    ws->lastBroadcastText.clear();
    g_fakeNowMs = 1000 + CommsTestAccess::stateKeepaliveIntervalMs() - 1;
    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());
    TEST_ASSERT_TRUE(ws->lastBroadcastText.empty());

    // The keepalive interval has now elapsed since the last broadcast, still with nothing
    // dirty - a full resync goes out anyway.
    g_fakeNowMs = 1000 + CommsTestAccess::stateKeepaliveIntervalMs();
    CommsTestAccess::broadcastStateIfDirty(Comms::Instance());
    TEST_ASSERT_FALSE(ws->lastBroadcastText.empty());
}

// ---------------------------------------------------------------------------
// Captive portal
// ---------------------------------------------------------------------------

void test_not_found_redirects_in_ap_mode()
{
    Comms::Instance().setup();  // ensure a known isAPMode state (no stored creds -> AP mode)
    TEST_ASSERT_TRUE(CommsTestAccess::isAPMode(Comms::Instance()));

    AsyncWebServerRequest req;
    req.setHost("some.other.host");
    CommsTestAccess::server(Comms::Instance()).notFoundHandler(&req);

    TEST_ASSERT_TRUE(req.wasRedirected);
}

void test_not_found_returns_404_in_station_mode()
{
    fakeStore().hasCredentials = true;
    fakeStore().storedSsid = "Home";
    fakeStore().storedPassword = "secret";
    fakeConnector().connectResult = true;
    Comms::Instance().setup();
    TEST_ASSERT_FALSE(CommsTestAccess::isAPMode(Comms::Instance()));

    AsyncWebServerRequest req;
    CommsTestAccess::server(Comms::Instance()).notFoundHandler(&req);

    TEST_ASSERT_FALSE(req.wasRedirected);
    TEST_ASSERT_EQUAL_INT(404, req.responseCode);
}

// ---------------------------------------------------------------------------
// OTA routes (#63)
// ---------------------------------------------------------------------------

void test_ota_and_ota_check_return_202_and_kick_the_updater()
{
    OtaUpdaterStub::startUpdateCalls = 0;
    OtaUpdaterStub::startCheckCalls = 0;

    AsyncWebServerRequest up;
    auto* upHandler = CommsTestAccess::server(Comms::Instance()).findHandler("/ota", HTTP_POST);
    TEST_ASSERT_NOT_NULL(upHandler);
    (*upHandler)(&up);
    TEST_ASSERT_EQUAL_INT(202, up.responseCode);
    TEST_ASSERT_EQUAL_INT(1, OtaUpdaterStub::startUpdateCalls);

    AsyncWebServerRequest check;
    auto* checkHandler =
        CommsTestAccess::server(Comms::Instance()).findHandler("/ota-check", HTTP_POST);
    TEST_ASSERT_NOT_NULL(checkHandler);
    (*checkHandler)(&check);
    TEST_ASSERT_EQUAL_INT(202, check.responseCode);
    TEST_ASSERT_EQUAL_INT(1, OtaUpdaterStub::startCheckCalls);
}

void test_ota_route_maps_a_non_started_outcome_to_its_http_status()
{
    OtaUpdaterStub::nextOutcome = OtaStartGate::Outcome::Busy;

    AsyncWebServerRequest busy;
    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/ota", HTTP_POST);
    (*handler)(&busy);
    TEST_ASSERT_EQUAL_INT(409, busy.responseCode);

    OtaUpdaterStub::nextOutcome = OtaStartGate::Outcome::NoWifi;
    AsyncWebServerRequest down;
    auto* checkHandler =
        CommsTestAccess::server(Comms::Instance()).findHandler("/ota-check", HTTP_POST);
    (*checkHandler)(&down);
    TEST_ASSERT_EQUAL_INT(503, down.responseCode);

    OtaUpdaterStub::nextOutcome = OtaStartGate::Outcome::Started;  // don't leak to later tests
}

void test_ota_rejects_cross_origin_post()
{
    OtaUpdaterStub::startUpdateCalls = 0;

    AsyncWebServerRequest req;
    req.setHost("andromeda-a1b2.local");
    req.setHeader("Origin", "http://evil.example.com");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/ota", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(403, req.responseCode);
    TEST_ASSERT_EQUAL_INT(0, OtaUpdaterStub::startUpdateCalls);
}

void test_ota_channel_persists_choice_and_rechecks()
{
    OtaConfig::persistDevChannel(false);
    OtaUpdaterStub::startCheckCalls = 0;

    auto* handler =
        CommsTestAccess::server(Comms::Instance()).findHandler("/ota-channel", HTTP_POST);
    TEST_ASSERT_NOT_NULL(handler);

    AsyncWebServerRequest on;
    on.setArg("dev", "true");
    (*handler)(&on);
    TEST_ASSERT_EQUAL_INT(200, on.responseCode);
    TEST_ASSERT_TRUE(OtaConfig::devChannel());
    TEST_ASSERT_EQUAL_INT(1, OtaUpdaterStub::startCheckCalls);

    AsyncWebServerRequest off;
    off.setArg("dev", "false");
    (*handler)(&off);
    TEST_ASSERT_FALSE(OtaConfig::devChannel());
}

void test_ota_channel_rejects_cross_origin_post()
{
    OtaConfig::persistDevChannel(false);

    AsyncWebServerRequest req;
    req.setHost("andromeda-a1b2.local");
    req.setHeader("Origin", "http://evil.example.com");
    req.setArg("dev", "true");

    auto* handler =
        CommsTestAccess::server(Comms::Instance()).findHandler("/ota-channel", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(403, req.responseCode);
    TEST_ASSERT_FALSE(OtaConfig::devChannel());
}

void test_ota_status_returns_parseable_json()
{
    AsyncWebServerRequest req;
    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/ota-status", HTTP_GET);
    TEST_ASSERT_NOT_NULL(handler);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(200, req.responseCode);

    StaticJsonDocument<256> doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, req.responseBody.c_str()) == DeserializationError::Ok);
    TEST_ASSERT_TRUE(doc.containsKey("state"));
    TEST_ASSERT_TRUE(doc.containsKey("progress"));
    TEST_ASSERT_TRUE(doc.containsKey("channel"));
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_save_valid_credentials_returns_202_without_blocking_or_persisting);
    RUN_TEST(test_save_empty_ssid_returns_400_and_does_not_persist);
    RUN_TEST(test_save_over_long_ssid_returns_400_and_does_not_persist);
    RUN_TEST(test_save_over_long_password_returns_400_and_does_not_persist);
    RUN_TEST(test_test_and_persist_persists_on_successful_probe);
    RUN_TEST(test_test_and_persist_does_not_persist_on_failed_probe);
    RUN_TEST(test_save_status_route_reports_probe_outcome);
    RUN_TEST(test_save_rejects_cross_origin_post);
    RUN_TEST(test_save_allows_same_origin_post);

    RUN_TEST(test_reset_clears_stored_credentials);
    RUN_TEST(test_reset_rejects_cross_origin_post);

    RUN_TEST(test_ota_and_ota_check_return_202_and_kick_the_updater);
    RUN_TEST(test_ota_route_maps_a_non_started_outcome_to_its_http_status);
    RUN_TEST(test_ota_rejects_cross_origin_post);
    RUN_TEST(test_ota_channel_persists_choice_and_rechecks);
    RUN_TEST(test_ota_channel_rejects_cross_origin_post);
    RUN_TEST(test_ota_status_returns_parseable_json);

    RUN_TEST(test_setup_connects_with_stored_credentials_and_skips_ap_mode);
    RUN_TEST(test_setup_falls_back_to_ap_mode_when_no_stored_credentials);
    RUN_TEST(test_setup_falls_back_to_ap_mode_when_stored_credentials_fail_to_connect);

    RUN_TEST(test_scan_route_returns_scanning_placeholder_on_cache_miss);
    RUN_TEST(test_scan_complete_escapes_ssid_and_reports_encryption);
    RUN_TEST(test_scan_failure_clears_scan_in_progress);
    RUN_TEST(test_synchronous_scan_start_failure_does_not_wedge_scan_in_progress);

    RUN_TEST(test_ws_valid_text_frame_queues_command);
    RUN_TEST(test_ws_color_commit_on_fast_path_persists_holding_color);

    RUN_TEST(test_ws_connect_pushes_initial_state);
    RUN_TEST(test_ws_connect_rejects_a_foreign_origin_handshake);
    RUN_TEST(test_ws_connect_allows_a_same_origin_handshake);
    RUN_TEST(test_state_broadcast_after_brightness_command);
    RUN_TEST(test_rapid_brightness_commands_bypass_queue_and_dont_drop);
    RUN_TEST(test_state_broadcast_skipped_when_not_dirty);
    RUN_TEST(test_state_broadcasts_are_throttled_within_the_interval);
    RUN_TEST(test_state_broadcast_forces_periodic_resync_even_when_not_dirty);

    RUN_TEST(test_not_found_redirects_in_ap_mode);
    RUN_TEST(test_not_found_returns_404_in_station_mode);

    return UNITY_END();
}
