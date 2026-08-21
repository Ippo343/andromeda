#pragma once

#include <ArduinoLog.h>
#include <FastLED.h>

#include <limits>

// Use this macro instead of calling FastLED.show()
// So that we automatically count every frame, no matter where it's called from.
// This fixes the annoying problem that the FPS of animations could not be measured
// as only the main loop was being counted.
#define FASTLED_SHOW()                         \
    do {                                       \
        FastLED.show();                        \
        PerformanceMonitor::Instance().tick(); \
    } while (0)

// Simple performance monitor.
// Call tick() every time a frame is rendered (FastLED.show())
// and then read the fps()
class PerformanceMonitor
{
   public:
    static inline PerformanceMonitor& Instance()
    {
        static PerformanceMonitor instance;
        return instance;
    }

    PerformanceMonitor(const PerformanceMonitor&) = delete;
    PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;

    inline void tick()
    {
        unsigned short now = millis();

        if (lastFrameTime != 0)
        {
            frameTimes[frameIndex] = now - lastFrameTime;
            frameIndex = (frameIndex + 1) & INDEX_MASK;  // Fast power-of-2 modulo
        }

        lastFrameTime = now;
    }

    float fps()
    {
        unsigned short totalTime = 0;

        for (size_t i = 0; i < SAMPLE_COUNT; i++) { totalTime += frameTimes[i]; }

        if (totalTime == 0) { return std::numeric_limits<float>::quiet_NaN(); }
        else
        {
            float avgFrameTime = totalTime / (float)SAMPLE_COUNT;
            return 1000.0f / avgFrameTime;
        }
    }

    void stat() { Log.noticeln("FPS: %F", fps()); }

    // Called by the main loop to stop the performance monitor when the mirror is off.
    // Otherwise, if nothing is drawn then there is no call to show(), which means the buffer
    // never changes, which means the FPS counter on the web hangs.
    void stop()
    {
        for (size_t i = 0; i < SAMPLE_COUNT; i++) { frameTimes[i] = 0; }
        lastFrameTime = 0;
        frameIndex = 0;
    }

   private:
    PerformanceMonitor() = default;

    // Power of 2 for fast modulo: did you know that using a power of 2 allows for a much faster
    // modulo operation? If N is a power of 2, then (x % N) is equivalent to (x & (N - 1)). I
    // didn't. Thanks Claude!
    static constexpr size_t SAMPLE_COUNT = 128;
    static constexpr size_t INDEX_MASK = SAMPLE_COUNT - 1;

    unsigned short frameTimes[SAMPLE_COUNT] = {0};  // Circular buffer of frame intervals
    unsigned short lastFrameTime = 0;               // Last frame timestamp
    size_t frameIndex = 0;                          // Current position in buffer

#ifdef UNIT_TEST
    // Test-only access so native unit tests can fill a synthetic sample
    // buffer directly instead of depending on real elapsed millis() between
    // tick() calls (which would make fps() tests either flaky or slow).
    friend class PerformanceMonitorTestAccess;
#endif
};
