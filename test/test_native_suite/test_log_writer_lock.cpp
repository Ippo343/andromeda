// Native unit tests for include/log-writer-lock.h - the mutual exclusion
// SimpleFileLog::write() (loggers.h) holds for its whole body, including
// checkRotation(), so no other FreeRTOS task can be mid-`_logFile.write(c)` while
// rotation closes/renames/reopens the shared File out from under it. loggers.h itself
// never compiles natively (it pulls LittleFS/ArduinoLog); this covers the lock
// primitive it delegates to (a std::mutex-backed stand-in here, a FreeRTOS semaphore
// on-device - see the header).
#include <unity.h>

#include "log-writer-lock.h"

void test_log_writer_lock_setUp() {}
void test_log_writer_lock_tearDown() {}

// Good weather: a single writer acquires and releases cleanly, and the write it guards
// happens - mirrors the normal, uncontended SimpleFileLog::write() call.
void test_single_writer_acquires_and_releases_cleanly()
{
    LogWriterLock lock;
    bool wroteWhileHeld = false;

    LogWriterLockGuard guard(lock);
    wroteWhileHeld = true;  // stands in for _logFile.write(c) happening under the lock

    TEST_ASSERT_TRUE(wroteWhileHeld);
    TEST_ASSERT_EQUAL_INT(1, lock.maxConcurrentHolders());
}

// A lock released after one holder must be immediately available to the next -
// checkRotation() finishing must not leave the lock stuck held.
void test_lock_is_reusable_after_release()
{
    LogWriterLock lock;

    lock.acquire();
    lock.release();

    TEST_ASSERT_TRUE(lock.tryAcquire());
    lock.release();
    TEST_ASSERT_EQUAL_INT(1, lock.maxConcurrentHolders());
}

// Bad weather: a second task's write() landing while the first is still mid-write
// (checkRotation()'s close()/rename()/open() sequence, in the real bug) must not be let
// in - tryAcquire() standing in for the non-blocking probe a reentrant attempt from
// inside that held critical section would make. Only one holder is ever observed.
void test_reentrant_acquire_is_rejected_while_held()
{
    LogWriterLock lock;

    TEST_ASSERT_TRUE(lock.tryAcquire());   // outer write(), mid checkRotation()
    TEST_ASSERT_FALSE(lock.tryAcquire());  // another task's write() racing it
    TEST_ASSERT_EQUAL_INT(1, lock.maxConcurrentHolders());

    lock.release();
    TEST_ASSERT_TRUE(lock.tryAcquire());  // free again once the outer write() finishes
    lock.release();

    TEST_ASSERT_EQUAL_INT(1, lock.maxConcurrentHolders());
}

void run_test_log_writer_lock_tests()
{
    RUN_TEST(test_single_writer_acquires_and_releases_cleanly);
    RUN_TEST(test_lock_is_reusable_after_release);
    RUN_TEST(test_reentrant_acquire_is_rejected_while_held);
}
