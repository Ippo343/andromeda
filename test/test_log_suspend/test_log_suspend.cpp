// Native unit tests for include/log-suspend.h - the drain handshake between
// SimpleFileLog::write() and OtaUpdater's LittleFS.end() (#63). loggers.h
// itself never compiles natively (it pulls LittleFS/ArduinoLog); this covers
// the pure state machine it delegates to.
#include <unity.h>

#include "log-suspend.h"

void setUp() { LogSuspend::resetForTest(); }
void tearDown() { LogSuspend::resetForTest(); }

// Good weather: with no suspend in progress, a write acquires a slot, releases
// it, and the in-flight count returns to zero. Normal logging is untouched.
void test_begin_end_write_round_trips()
{
    TEST_ASSERT_EQUAL_INT(0, LogSuspend::writersInFlight());

    TEST_ASSERT_TRUE(LogSuspend::beginWrite());
    TEST_ASSERT_EQUAL_INT(1, LogSuspend::writersInFlight());

    LogSuspend::endWrite();
    TEST_ASSERT_EQUAL_INT(0, LogSuspend::writersInFlight());
}

void test_nested_writers_are_counted()
{
    TEST_ASSERT_TRUE(LogSuspend::beginWrite());
    TEST_ASSERT_TRUE(LogSuspend::beginWrite());
    TEST_ASSERT_EQUAL_INT(2, LogSuspend::writersInFlight());

    LogSuspend::endWrite();
    LogSuspend::endWrite();
    TEST_ASSERT_EQUAL_INT(0, LogSuspend::writersInFlight());
}

// Bad weather: once a suspend begins, no new writer may enter - so a log line
// can't slip onto the filesystem after OtaUpdater has committed to unmounting
// it.
void test_no_new_writer_after_suspend_begins()
{
    LogSuspend::beginSuspend();
    TEST_ASSERT_FALSE(LogSuspend::beginWrite());
    TEST_ASSERT_EQUAL_INT(0, LogSuspend::writersInFlight());
}

// Bad weather: suspendAndDrain() must NOT report drained while a writer is
// still inside its write body. Modelled by a yield that releases the writer
// only after a few spins - drained() is false until then.
void test_suspend_and_drain_waits_for_an_in_flight_writer()
{
    TEST_ASSERT_TRUE(LogSuspend::beginWrite());  // a writer is mid-write()

    int spins = 0;
    auto yield = [&spins]()
    {
        TEST_ASSERT_FALSE(LogSuspend::drained());  // still not safe to LittleFS.end()
        if (++spins == 3) LogSuspend::endWrite();  // the writer finally leaves
    };

    TEST_ASSERT_TRUE(LogSuspend::suspendAndDrain(yield));
    TEST_ASSERT_EQUAL_INT(3, spins);
    TEST_ASSERT_TRUE(LogSuspend::drained());
    TEST_ASSERT_EQUAL_INT(0, LogSuspend::writersInFlight());
}

// Bad weather: a genuinely stuck writer must not hang the OTA task forever -
// the spin is bounded and reports failure.
void test_suspend_and_drain_bails_on_a_stuck_writer()
{
    TEST_ASSERT_TRUE(LogSuspend::beginWrite());  // never released

    int spins = 0;
    auto yield = [&spins]() { spins++; };

    TEST_ASSERT_FALSE(LogSuspend::suspendAndDrain(yield, 10));
    TEST_ASSERT_EQUAL_INT(10, spins);
    // The flag is still set: no writer can enter even though we gave up
    // waiting, so a straggler can't corrupt the image after LittleFS.end().
    TEST_ASSERT_FALSE(LogSuspend::beginWrite());
}

// A rejected beginWrite() (suspend already set) must not leave the count
// inflated - otherwise suspendAndDrain() would spin forever on a phantom
// writer.
void test_rejected_begin_write_does_not_inflate_the_count()
{
    LogSuspend::beginSuspend();
    for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(LogSuspend::beginWrite());
    TEST_ASSERT_EQUAL_INT(0, LogSuspend::writersInFlight());
    TEST_ASSERT_TRUE(LogSuspend::drained());
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_end_write_round_trips);
    RUN_TEST(test_nested_writers_are_counted);
    RUN_TEST(test_no_new_writer_after_suspend_begins);
    RUN_TEST(test_suspend_and_drain_waits_for_an_in_flight_writer);
    RUN_TEST(test_suspend_and_drain_bails_on_a_stuck_writer);
    RUN_TEST(test_rejected_begin_write_does_not_inflate_the_count);
    return UNITY_END();
}
