// Native unit tests for include/ota-retry-schedule.h - the retry cadence for
// main.cpp's OtaAutoCheck task (#63). Covers the two bugs the audit found in
// the inline version: the filesystem self-repair loop falling back to the
// daily cadence after a single *spawned* (not succeeded) attempt, and a boot
// where WiFi isn't up yet losing a full day before the next check.
#include <unity.h>

#include "ota-retry-schedule.h"

using OtaStartGate::Outcome;

void setUp() {}
void tearDown() {}

// --- steady state: a real check actually ran -------------------------------

void test_normal_check_started_settles_on_the_daily_cadence()
{
    // Good weather: WiFi up, a normal (non-repair) check ran to completion.
    TEST_ASSERT_EQUAL_UINT32(OtaRetrySchedule::STEADY_STATE_DELAY_MS,
                             OtaRetrySchedule::nextDelayMs(false, Outcome::Started, 0));
    // Unaffected by whatever the previous delay happened to be.
    TEST_ASSERT_EQUAL_UINT32(OtaRetrySchedule::STEADY_STATE_DELAY_MS,
                             OtaRetrySchedule::nextDelayMs(false, Outcome::Started,
                                                           OtaRetrySchedule::MAX_RETRY_DELAY_MS));
}

// --- bug #1: filesystem repair must never fall back to the daily cadence ---

void test_fs_repair_never_reaches_the_daily_cadence_even_after_starting()
{
    // Bad weather: the repair worker was *spawned* (Started) but that says
    // nothing about whether it succeeded - the old code cleared its "still
    // damaged" flag right here and fell back to a once-a-day check with no
    // web UI to recover through. It must keep retrying instead.
    uint32_t delay = 0;
    for (int i = 0; i < 10; ++i)
    {
        delay = OtaRetrySchedule::nextDelayMs(true, Outcome::Started, delay);
        TEST_ASSERT_TRUE_MESSAGE(delay <= OtaRetrySchedule::MAX_RETRY_DELAY_MS,
                                 "fs-damaged retry must stay capped, never reach 24h");
    }
}

void test_fs_repair_backs_off_but_stays_capped_on_repeated_failure()
{
    // Bad weather: the worker couldn't even spawn (e.g. WiFi still down).
    // Should still back off rather than hammering GitHub every minute, but
    // never grow into the daily cadence.
    uint32_t delay = 0;
    for (int i = 0; i < 10; ++i)
    {
        delay = OtaRetrySchedule::nextDelayMs(true, Outcome::NoWifi, delay);
        TEST_ASSERT_TRUE(delay <= OtaRetrySchedule::MAX_RETRY_DELAY_MS);
    }
    TEST_ASSERT_EQUAL_UINT32(OtaRetrySchedule::MAX_RETRY_DELAY_MS, delay);
}

// --- bug #2: a boot with WiFi not yet up must retry soon, not in 24h -------

void test_not_started_outcome_backs_off_instead_of_waiting_a_full_day()
{
    // Bad weather: startCheck() couldn't run (most commonly NoWifi at
    // boot+10s, e.g. still in AP setup mode). The first retry must be soon.
    uint32_t delay = OtaRetrySchedule::nextDelayMs(false, Outcome::NoWifi, 0);
    TEST_ASSERT_TRUE_MESSAGE(delay <= 5ULL * 60 * 1000,
                             "a boot that couldn't check yet must retry within minutes, "
                             "not disappear for a full day");
    TEST_ASSERT_NOT_EQUAL(OtaRetrySchedule::STEADY_STATE_DELAY_MS, delay);
}

void test_not_started_outcome_backs_off_and_stays_capped()
{
    uint32_t delay = 0;
    for (int i = 0; i < 10; ++i)
    {
        delay = OtaRetrySchedule::nextDelayMs(false, Outcome::LowHeap, delay);
        TEST_ASSERT_TRUE(delay <= OtaRetrySchedule::MAX_RETRY_DELAY_MS);
    }
}

// --- once WiFi comes up mid-backoff, the very next successful check settles

void test_recovering_to_started_returns_to_daily_cadence_immediately()
{
    uint32_t delay = OtaRetrySchedule::nextDelayMs(false, Outcome::NoWifi, 0);
    delay = OtaRetrySchedule::nextDelayMs(false, Outcome::NoWifi, delay);
    // WiFi comes up; the next check actually runs.
    delay = OtaRetrySchedule::nextDelayMs(false, Outcome::Started, delay);
    TEST_ASSERT_EQUAL_UINT32(OtaRetrySchedule::STEADY_STATE_DELAY_MS, delay);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_normal_check_started_settles_on_the_daily_cadence);
    RUN_TEST(test_fs_repair_never_reaches_the_daily_cadence_even_after_starting);
    RUN_TEST(test_fs_repair_backs_off_but_stays_capped_on_repeated_failure);
    RUN_TEST(test_not_started_outcome_backs_off_instead_of_waiting_a_full_day);
    RUN_TEST(test_not_started_outcome_backs_off_and_stays_capped);
    RUN_TEST(test_recovering_to_started_returns_to_daily_cadence_immediately);
    return UNITY_END();
}
