#include <unity.h>

#include "wifi-recovery.h"

void setUp() {}
void tearDown() {}

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

    // Keep doubling past the cap - 8000 -> 16000 -> 32000 (clamped to 60000) -> stays 60000.
    t += 8000;
    r.tick(t);
    t += 16000;
    r.tick(t);
    t += 32000;
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

    return UNITY_END();
}
