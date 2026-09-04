// Native unit tests for include/ota-start-gate.h - the pure entry gate that
// decides whether OtaUpdater::startCheck() / startUpdate() spawns a worker,
// and what HTTP status src/comms.cpp answers with (#63).
#include <unity.h>

#include <initializer_list>

#include "ota-start-gate.h"

using OtaStartGate::Outcome;

void test_ota_start_gate_setUp() {}
void test_ota_start_gate_tearDown() {}

// MIN_FREE_HEAP mirror for the tests (src/ota-updater.cpp's constant).
static constexpr uint32_t kMinHeap = 60 * 1024;

void test_evaluate_started_when_everything_is_clear()
{
    // Good weather: WiFi up, plenty of heap, no task running.
    TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Started),
                      static_cast<int>(OtaStartGate::evaluate(true, 120 * 1024, kMinHeap, false)));
}

void test_evaluate_reports_no_wifi_first()
{
    // Bad weather: no station connection - reported ahead of every other
    // blocker so the HTTP status is the actionable one.
    TEST_ASSERT_EQUAL(static_cast<int>(Outcome::NoWifi),
                      static_cast<int>(OtaStartGate::evaluate(false, 8 * 1024, kMinHeap, true)));
}

void test_evaluate_reports_low_heap()
{
    // Bad weather: heap one byte under the floor.
    TEST_ASSERT_EQUAL(
        static_cast<int>(Outcome::LowHeap),
        static_cast<int>(OtaStartGate::evaluate(true, kMinHeap - 1, kMinHeap, false)));
    // Exactly at the floor is allowed.
    TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Started),
                      static_cast<int>(OtaStartGate::evaluate(true, kMinHeap, kMinHeap, false)));
}

void test_evaluate_reports_busy_when_a_task_is_in_flight()
{
    // Bad weather: WiFi + heap fine, but a check/update worker already exists.
    TEST_ASSERT_EQUAL(static_cast<int>(Outcome::Busy),
                      static_cast<int>(OtaStartGate::evaluate(true, 120 * 1024, kMinHeap, true)));
}

void test_http_status_mapping()
{
    TEST_ASSERT_EQUAL(202, OtaStartGate::httpStatus(Outcome::Started));
    TEST_ASSERT_EQUAL(409, OtaStartGate::httpStatus(Outcome::Busy));
    TEST_ASSERT_EQUAL(503, OtaStartGate::httpStatus(Outcome::NoWifi));
    TEST_ASSERT_EQUAL(503, OtaStartGate::httpStatus(Outcome::LowHeap));
}

void test_every_outcome_has_a_nonempty_message()
{
    for (Outcome o : {Outcome::Started, Outcome::Busy, Outcome::NoWifi, Outcome::LowHeap})
    {
        const char* m = OtaStartGate::message(o);
        TEST_ASSERT_NOT_NULL(m);
        TEST_ASSERT_TRUE(m[0] != '\0');
    }
}

void run_test_ota_start_gate_tests()
{
    RUN_TEST(test_evaluate_started_when_everything_is_clear);
    RUN_TEST(test_evaluate_reports_no_wifi_first);
    RUN_TEST(test_evaluate_reports_low_heap);
    RUN_TEST(test_evaluate_reports_busy_when_a_task_is_in_flight);
    RUN_TEST(test_http_status_mapping);
    RUN_TEST(test_every_outcome_has_a_nonempty_message);
}
