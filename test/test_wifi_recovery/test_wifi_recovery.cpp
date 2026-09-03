#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>

#include "wifi-recovery.h"

void setUp() {}
void tearDown() {}

// Plain-text guard: src/wifi-esp-adapters.cpp is excluded from the native
// build (it polls WiFi.status() in real time), so this is the only way to pin
// that both AP-mode entry points reapply the SoftAP config + captive-portal
// DNS option through one shared helper rather than a bare WiFi.mode(WIFI_AP).
// The rejoin path dropping the DNS option was the #76 gap where the portal
// popup stopped appearing after the first failed rejoin.
void test_ap_mode_entry_points_share_the_config_helper()
{
    std::ifstream f("src/wifi-esp-adapters.cpp");
    TEST_ASSERT_TRUE_MESSAGE(f.is_open(), "could not open src/wifi-esp-adapters.cpp");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();

    // The helper exists and does the two things that must not be forgotten.
    TEST_ASSERT_TRUE(src.find("void reapplyApConfig()") != std::string::npos);
    TEST_ASSERT_TRUE(src.find("WiFi.softAP(") != std::string::npos);
    TEST_ASSERT_TRUE(src.find("esp_netif_set_dns_info(") != std::string::npos);

    // Both callers go through it, and no other line falls back to a bare
    // WiFi.mode(WIFI_AP) that would skip the DNS option.
    size_t calls = 0;
    for (size_t p = src.find("reapplyApConfig()"); p != std::string::npos;
         p = src.find("reapplyApConfig()", p + 1))
        calls++;
    TEST_ASSERT_TRUE_MESSAGE(calls >= 3, "expected the definition + 2 call sites");

    // The ";" excludes the two prose mentions in comments; "WiFi.mode(WIFI_AP_STA);"
    // won't match because of the trailing ")". The one legitimate statement is
    // inside reapplyApConfig() itself.
    size_t bareApMode = 0;
    for (size_t p = src.find("WiFi.mode(WIFI_AP);"); p != std::string::npos;
         p = src.find("WiFi.mode(WIFI_AP);", p + 1))
        bareApMode++;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)bareApMode,
                                  "a WiFi.mode(WIFI_AP); outside reapplyApConfig() would skip the "
                                  "captive-portal DNS option");
}

void test_no_action_when_never_disconnected()
{
    WifiRecovery r;
    TEST_ASSERT_TRUE(r.tick(0) == WifiRecovery::Action::None);
    TEST_ASSERT_TRUE(r.tick(100000) == WifiRecovery::Action::None);
}

void test_first_tick_after_disconnect_reconnects_immediately()
{
    WifiRecovery r;
    r.onDisconnected(1000);
    // nextAttemptAt_ is seeded to the disconnect time itself - the first retry doesn't wait
    // out a full backoff window.
    TEST_ASSERT_TRUE(r.tick(1000) == WifiRecovery::Action::Reconnect);
}

void test_backoff_doubles_and_caps_at_max()
{
    WifiRecovery r;
    milliseconds_t t = 0;
    r.onDisconnected(t);

    TEST_ASSERT_TRUE(r.tick(t) == WifiRecovery::Action::Reconnect);  // schedules +1000
    t += WifiRecovery::INITIAL_BACKOFF_MS;
    TEST_ASSERT_TRUE(r.tick(t) == WifiRecovery::Action::Reconnect);  // schedules +2000
    t += 2000;
    TEST_ASSERT_TRUE(r.tick(t) == WifiRecovery::Action::Reconnect);  // schedules +4000
    t += 4000;
    TEST_ASSERT_TRUE(r.tick(t) == WifiRecovery::Action::Reconnect);  // schedules +8000

    // Keep doubling past the cap - 8000 -> 15000 (clamped) -> stays 15000. All of this
    // still fits inside AP_FALLBACK_DEAD_TIME_MS, so the cap is actually observable.
    t += 8000;
    r.tick(t);  // schedules +15000
    t += 15000;
    WifiRecovery::Action a = r.tick(t);
    TEST_ASSERT_TRUE(a == WifiRecovery::Action::Reconnect);

    // One more attempt should now be scheduled exactly MAX_BACKOFF_MS out, not further.
    t += WifiRecovery::MAX_BACKOFF_MS - 1;
    TEST_ASSERT_TRUE(r.tick(t) == WifiRecovery::Action::None);
    t += 1;
    TEST_ASSERT_TRUE(r.tick(t) == WifiRecovery::Action::Reconnect);
}

void test_no_reconnect_before_next_attempt_is_due()
{
    WifiRecovery r;
    r.onDisconnected(0);
    TEST_ASSERT_TRUE(r.tick(0) == WifiRecovery::Action::Reconnect);  // schedules +1000
    TEST_ASSERT_TRUE(r.tick(500) == WifiRecovery::Action::None);
    TEST_ASSERT_TRUE(r.tick(999) == WifiRecovery::Action::None);
    TEST_ASSERT_TRUE(r.tick(1000) == WifiRecovery::Action::Reconnect);
}

void test_ap_fallback_after_dead_time_elapses()
{
    WifiRecovery r;
    r.onDisconnected(0);
    TEST_ASSERT_TRUE(r.tick(WifiRecovery::AP_FALLBACK_DEAD_TIME_MS - 1) !=
                     WifiRecovery::Action::EnterAPMode);
    TEST_ASSERT_TRUE(r.tick(WifiRecovery::AP_FALLBACK_DEAD_TIME_MS) ==
                     WifiRecovery::Action::EnterAPMode);
}

void test_ap_fallback_signaled_only_once()
{
    WifiRecovery r;
    r.onDisconnected(0);
    TEST_ASSERT_TRUE(r.tick(WifiRecovery::AP_FALLBACK_DEAD_TIME_MS) ==
                     WifiRecovery::Action::EnterAPMode);
    // Same outage, later tick: no repeated EnterAPMode/Reconnect - the caller is expected to
    // have actually switched to AP mode by now.
    TEST_ASSERT_TRUE(r.tick(WifiRecovery::AP_FALLBACK_DEAD_TIME_MS + 10000) ==
                     WifiRecovery::Action::None);
}

void test_onConnected_resets_backoff_and_stops_further_actions()
{
    WifiRecovery r;
    r.onDisconnected(0);
    r.tick(0);  // backoff has advanced past INITIAL_BACKOFF_MS
    r.onConnected();

    TEST_ASSERT_TRUE(r.tick(1000) == WifiRecovery::Action::None);
    TEST_ASSERT_TRUE(r.tick(1000000) == WifiRecovery::Action::None);
}

void test_reconnect_after_reconnect_starts_backoff_over()
{
    WifiRecovery r;
    r.onDisconnected(0);
    r.tick(0);        // backoff now doubled to 2000
    r.onConnected();  // resets
    r.onDisconnected(5000);
    // Fresh outage - first attempt is immediate again, not picking up the old doubled value.
    TEST_ASSERT_TRUE(r.tick(5000) == WifiRecovery::Action::Reconnect);
    TEST_ASSERT_TRUE(r.tick(5000 + WifiRecovery::INITIAL_BACKOFF_MS - 1) ==
                     WifiRecovery::Action::None);
}

void test_onDisconnected_is_idempotent_mid_outage()
{
    WifiRecovery r;
    r.onDisconnected(0);
    r.tick(0);  // schedules the next attempt at +1000
    // A second STA_DISCONNECTED event for the same ongoing outage (e.g. a reconnect attempt
    // itself failing again) must not reset the dead-time clock back to "now" - that would
    // make the AP-mode fallback unreachable for an outage where reconnects keep firing.
    r.onDisconnected(500);
    TEST_ASSERT_TRUE(r.tick(WifiRecovery::AP_FALLBACK_DEAD_TIME_MS) ==
                     WifiRecovery::Action::EnterAPMode);
}

void test_millis_rollover_does_not_break_scheduling()
{
    WifiRecovery r;
    milliseconds_t nearWrap = ~0UL - 500;
    r.onDisconnected(nearWrap);
    TEST_ASSERT_TRUE(r.tick(nearWrap) == WifiRecovery::Action::Reconnect);  // schedules +1000

    // nearWrap + 1000 wraps past ~0UL back to a small value.
    milliseconds_t afterWrap = nearWrap + 1000;
    TEST_ASSERT_TRUE(afterWrap < nearWrap);  // sanity: this really did wrap
    TEST_ASSERT_TRUE(r.tick(afterWrap) == WifiRecovery::Action::Reconnect);
}

void test_jitter_is_added_to_backoff()
{
    WifiRecovery r;
    r.onDisconnected(0);
    r.tick(0, 500);  // next attempt scheduled at 0 + INITIAL_BACKOFF_MS + 500
    TEST_ASSERT_TRUE(r.tick(WifiRecovery::INITIAL_BACKOFF_MS + 499) == WifiRecovery::Action::None);
    TEST_ASSERT_TRUE(r.tick(WifiRecovery::INITIAL_BACKOFF_MS + 500) ==
                     WifiRecovery::Action::Reconnect);
}

// --- postEvent() queue: the WiFi-task -> monitor-task handoff --------------

// Good weather: an outage posted through the queue drives exactly the same
// reconnect schedule as a direct onDisconnected() call.
void test_queued_disconnect_drives_the_reconnect_schedule()
{
    WifiRecovery r;
    r.postEvent(WifiRecovery::Event::Disconnected, 1000);

    // tick() drains the event first, then decides - first tick reconnects
    // immediately, next attempt is scheduled +INITIAL_BACKOFF_MS.
    TEST_ASSERT_TRUE(r.tick(1000) == WifiRecovery::Action::Reconnect);
    TEST_ASSERT_TRUE(r.tick(1000 + WifiRecovery::INITIAL_BACKOFF_MS - 1) ==
                     WifiRecovery::Action::None);
    TEST_ASSERT_TRUE(r.tick(1000 + WifiRecovery::INITIAL_BACKOFF_MS) ==
                     WifiRecovery::Action::Reconnect);
}

// Bad weather: the interleaving the direct-call version raced on - tick()
// evaluates mid-outage while the WiFi task lands a Connected. Through the
// queue, tick() drains BOTH events before deciding, so a link that has come
// back is never told to reconnect.
void test_connect_landing_before_a_tick_cancels_the_pending_reconnect()
{
    WifiRecovery r;
    r.postEvent(WifiRecovery::Event::Disconnected, 1000);
    r.tick(1000);  // reconnect once, backoff now scheduled at +INITIAL_BACKOFF_MS

    // The link comes back before the backoff window is due.
    r.postEvent(WifiRecovery::Event::Connected, 1200);

    // The next tick, even well past when a reconnect would have been due, must
    // NOT fire one - the queue applied Connected first.
    TEST_ASSERT_TRUE(r.tick(1000 + WifiRecovery::MAX_BACKOFF_MS) == WifiRecovery::Action::None);
}

// Bad weather: a fresh 1-second glitch must start its own dead-time clock, not
// inherit the previous (recovered) outage's - otherwise it jumps straight to
// AP fallback.
void test_a_new_outage_after_recovery_does_not_inherit_the_old_dead_time()
{
    WifiRecovery r;
    r.postEvent(WifiRecovery::Event::Disconnected, 1000);
    r.tick(1000);
    r.postEvent(WifiRecovery::Event::Connected, 1100);
    r.tick(1100);

    // New outage 4 minutes later; tick 10s in - well under the 5-minute dead
    // time measured from THIS outage's start.
    const milliseconds_t newOutage = 1100 + 4UL * 60 * 1000;
    r.postEvent(WifiRecovery::Event::Disconnected, newOutage);
    TEST_ASSERT_TRUE(r.tick(newOutage + 10000) != WifiRecovery::Action::EnterAPMode);
}

// Events are applied in the order posted - a Disconnected then Connected in
// the same drain leaves the machine connected (last wins), and vice versa.
void test_queue_applies_events_in_order()
{
    WifiRecovery r;
    r.postEvent(WifiRecovery::Event::Disconnected, 1000);
    r.postEvent(WifiRecovery::Event::Connected, 1001);
    TEST_ASSERT_TRUE(r.tick(5000) == WifiRecovery::Action::None);  // ends connected

    r.postEvent(WifiRecovery::Event::Connected, 6000);
    r.postEvent(WifiRecovery::Event::Disconnected, 6001);
    TEST_ASSERT_TRUE(r.tick(6001) == WifiRecovery::Action::Reconnect);  // ends disconnected
}

int main(int argc, char** argv)
{
    UNITY_BEGIN();

    RUN_TEST(test_no_action_when_never_disconnected);
    RUN_TEST(test_first_tick_after_disconnect_reconnects_immediately);
    RUN_TEST(test_backoff_doubles_and_caps_at_max);
    RUN_TEST(test_no_reconnect_before_next_attempt_is_due);
    RUN_TEST(test_ap_fallback_after_dead_time_elapses);
    RUN_TEST(test_ap_fallback_signaled_only_once);
    RUN_TEST(test_onConnected_resets_backoff_and_stops_further_actions);
    RUN_TEST(test_reconnect_after_reconnect_starts_backoff_over);
    RUN_TEST(test_onDisconnected_is_idempotent_mid_outage);
    RUN_TEST(test_millis_rollover_does_not_break_scheduling);
    RUN_TEST(test_jitter_is_added_to_backoff);

    RUN_TEST(test_queued_disconnect_drives_the_reconnect_schedule);
    RUN_TEST(test_connect_landing_before_a_tick_cancels_the_pending_reconnect);
    RUN_TEST(test_a_new_outage_after_recovery_does_not_inherit_the_old_dead_time);
    RUN_TEST(test_queue_applies_events_in_order);

    RUN_TEST(test_ap_mode_entry_points_share_the_config_helper);

    return UNITY_END();
}
