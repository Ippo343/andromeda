#pragma once

// Mutual exclusion for SimpleFileLog::write() (loggers.h). Log.* is called from 5+
// FreeRTOS tasks (render/loopTask, async_tcp, WebServer, WiFi event task, OTA/recovery
// tasks) with no serialization between them otherwise. checkRotation() does
// `_logFile.close(); LittleFS.rename(...); _logFile = LittleFS.open(...)`, destroying and
// reassigning the shared File (shared_ptr) non-atomically - another task mid-
// `_logFile.write(c)` when that happens is a use-after-free/reassignment race, not just a
// garbled line. The 16KB rotation cap (SimpleFileLog::DEFAULT_MAX_LOG_BYTES) makes this
// fire often.
//
// This is a different, narrower problem than LogSuspend (log-suspend.h): LogSuspend
// drains in-flight writers once, before the OTA task's LittleFS.end() - it does nothing to
// serialize writers against each other the rest of the time. This lock does that, held for
// the whole body of SimpleFileLog::write() including checkRotation().
//
// No LittleFS/ArduinoLog/FreeRTOS deps in the interface so the native suite can exercise
// the real locking primitive rather than a stand-in: a std::mutex-backed implementation
// natively (UNIT_TEST), a non-recursive FreeRTOS semaphore on-device.

#include <atomic>

#ifdef UNIT_TEST
#include <mutex>
#else
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

class LogWriterLock
{
   public:
#ifndef UNIT_TEST
    LogWriterLock() : _sem(xSemaphoreCreateMutex()) {}
#endif

    // Blocks until the lock is free, then takes it.
    void acquire()
    {
#ifdef UNIT_TEST
        _mutex.lock();
#else
        xSemaphoreTake(_sem, portMAX_DELAY);
#endif
        recordAcquire();
    }

    // Non-blocking: returns false immediately if already held.
    bool tryAcquire()
    {
#ifdef UNIT_TEST
        if (!_mutex.try_lock()) return false;
#else
        if (xSemaphoreTake(_sem, 0) != pdTRUE) return false;
#endif
        recordAcquire();
        return true;
    }

    void release()
    {
        recordRelease();
#ifdef UNIT_TEST
        _mutex.unlock();
#else
        xSemaphoreGive(_sem);
#endif
    }

    // Test-only: the peak number of simultaneous holders observed since construction. A
    // correct lock never lets this exceed 1.
    int maxConcurrentHolders() const { return _maxHolders.load(std::memory_order_relaxed); }

   private:
    void recordAcquire()
    {
        int now = _holders.fetch_add(1, std::memory_order_acq_rel) + 1;
        int prevMax = _maxHolders.load(std::memory_order_relaxed);
        while (now > prevMax &&
               !_maxHolders.compare_exchange_weak(prevMax, now, std::memory_order_relaxed))
        {
        }
    }
    void recordRelease() { _holders.fetch_sub(1, std::memory_order_acq_rel); }

    std::atomic<int> _holders{0};
    std::atomic<int> _maxHolders{0};
#ifdef UNIT_TEST
    std::mutex _mutex;
#else
    SemaphoreHandle_t _sem;
#endif
};

// RAII helper: acquires in the constructor, releases in the destructor - so a lock held
// "for the whole body" of a function is just one guard declared at its top.
class LogWriterLockGuard
{
   public:
    explicit LogWriterLockGuard(LogWriterLock& lock) : _lock(lock) { _lock.acquire(); }
    ~LogWriterLockGuard() { _lock.release(); }

    LogWriterLockGuard(const LogWriterLockGuard&) = delete;
    LogWriterLockGuard& operator=(const LogWriterLockGuard&) = delete;

   private:
    LogWriterLock& _lock;
};
