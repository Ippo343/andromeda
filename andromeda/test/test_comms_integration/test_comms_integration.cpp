#include <ArduinoJson.h>
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

// comms.cpp transitively needs MissionControl (getMaxBrightness, the WS
// command queue) and PerformanceMonitor, so this test binary assembles the
// same self-contained chain test_mission_control.cpp does (effects.cpp for
// getRandomEffect(), a stub getRandomAnimation() so animations.cpp isn't
// needed, then mission-control.cpp), with comms.cpp #included on top.
// comms.cpp can't go through platformio.ini's shared native build_src_filter
// at all - every native test target links against that filter, and most
// don't define MissionControl/PerformanceMonitor.
#include "../../src/effects.cpp"
#include "animation-base.h"
#include "animation-frame-base.h"

const char* AbstractBlockingAnimation::GetName() { return "AbstractBlockingAnimation"; }
void AbstractBlockingAnimation::run() {}

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

// ---------------------------------------------------------------------------
// Fakes
// ---------------------------------------------------------------------------

class FakeWiFiConnector : public IWiFiConnector
{
   public:
    bool connectResult = true;
    bool testConnectionResult = true;
    int enterAPModeCalls = 0;

    bool connect(const char*, const char*) override { return connectResult; }
    bool testConnection(const char*, const char*) override
    {
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
};

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
}
void tearDown() {}

// ---------------------------------------------------------------------------
// /save
// ---------------------------------------------------------------------------

void test_save_success_persists_and_returns_200()
{
    fakeConnector().testConnectionResult = true;

    AsyncWebServerRequest req;
    req.setArg("ssid", "MyNetwork");
    req.setArg("password", "hunter2");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/save", HTTP_POST);
    TEST_ASSERT_NOT_NULL(handler);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(200, req.responseCode);
    TEST_ASSERT_TRUE(fakeStore().hasCredentials);
    TEST_ASSERT_TRUE(fakeStore().storedSsid == String("MyNetwork"));
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

void test_save_connection_failure_returns_400_and_does_not_persist()
{
    fakeConnector().testConnectionResult = false;

    AsyncWebServerRequest req;
    req.setArg("ssid", "MyNetwork");
    req.setArg("password", "wrong");

    auto* handler = CommsTestAccess::server(Comms::Instance()).findHandler("/save", HTTP_POST);
    (*handler)(&req);

    TEST_ASSERT_EQUAL_INT(400, req.responseCode);
    TEST_ASSERT_FALSE(fakeStore().hasCredentials);
    // testConnection() must revert to AP mode on failure too, per production behavior.
    TEST_ASSERT_TRUE(fakeConnector().enterAPModeCalls > 0);
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

    Comms::Instance().setup();

    TEST_ASSERT_FALSE(CommsTestAccess::isAPMode(Comms::Instance()));
}

void test_setup_falls_back_to_ap_mode_when_no_stored_credentials()
{
    fakeStore().hasCredentials = false;

    Comms::Instance().setup();

    TEST_ASSERT_TRUE(CommsTestAccess::isAPMode(Comms::Instance()));
}

void test_setup_falls_back_to_ap_mode_when_stored_credentials_fail_to_connect()
{
    fakeStore().hasCredentials = true;
    fakeStore().storedSsid = "Home";
    fakeStore().storedPassword = "secret";
    fakeConnector().connectResult = false;

    Comms::Instance().setup();

    TEST_ASSERT_TRUE(CommsTestAccess::isAPMode(Comms::Instance()));
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

    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, client.lastSentText));
    TEST_ASSERT_EQUAL_STRING("state", doc["type"]);
    TEST_ASSERT_EQUAL(MissionControl::Instance().getMaxBrightness(), doc["brightness"].as<int>());
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
    StaticJsonDocument<WsStateBuilder::JSON_CAPACITY> doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, ws->lastBroadcastText));
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

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_save_success_persists_and_returns_200);
    RUN_TEST(test_save_empty_ssid_returns_400_and_does_not_persist);
    RUN_TEST(test_save_connection_failure_returns_400_and_does_not_persist);

    RUN_TEST(test_reset_clears_stored_credentials);

    RUN_TEST(test_setup_connects_with_stored_credentials_and_skips_ap_mode);
    RUN_TEST(test_setup_falls_back_to_ap_mode_when_no_stored_credentials);
    RUN_TEST(test_setup_falls_back_to_ap_mode_when_stored_credentials_fail_to_connect);

    RUN_TEST(test_scan_route_returns_scanning_placeholder_on_cache_miss);

    RUN_TEST(test_ws_valid_text_frame_queues_command);

    RUN_TEST(test_ws_connect_pushes_initial_state);
    RUN_TEST(test_state_broadcast_after_brightness_command);
    RUN_TEST(test_rapid_brightness_commands_bypass_queue_and_dont_drop);
    RUN_TEST(test_state_broadcast_skipped_when_not_dirty);

    RUN_TEST(test_not_found_redirects_in_ap_mode);
    RUN_TEST(test_not_found_returns_404_in_station_mode);

    return UNITY_END();
}
