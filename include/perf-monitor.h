#pragma once

#include <ArduinoLog.h>
#include <FastLED.h>

#include <limits>

#include "utils.h"

// Optional per-frame callback, set only by the native runtime
// (src/native-runtime.cpp) to export the LED buffer to the browser
// visualizer bridge after every real frame. nullptr (its default, and the
// only value it ever has on firmware/test builds) makes this a single
// pointer check - effectively free everywhere except env:native_runtime.
using FrameCaptureHook = void (*)();
inline FrameCaptureHook g_frameCaptureHook = nullptr;

// Use this macro instead of calling FastLED.show()
// So that we automatically count every frame, no matter where it's called from.
// This fixes the annoying problem that the FPS of animations could not be measured
// as only the main loop was being counted.
#define FASTLED_SHOW()                                \
    do {                                              \
        FastLED.show();                               \
        PerformanceMonitor::Instance().tick();        \
        if (g_frameCaptureHook) g_frameCaptureHook(); \
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
        milliseconds_t now = millis();

        if (lastFrameTime != 0)
        {
            frameTimes[frameIndex] = now - lastFrameTime;
            frameIndex = (frameIndex + 1) & INDEX_MASK;  // Fast power-of-2 modulo
        }

        lastFrameTime = now;
    }

    float fps()
    {
        // Deliberately always averages over the full SAMPLE_COUNT, including any
        // still-zero slots at boot or right after stop() - a simple, cheap
        // running average that trades a little accuracy in that narrow window
        // for not having to track how many of the slots hold a real sample.
        uint32_t totalTime = 0;
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

    milliseconds_t frameTimes[SAMPLE_COUNT] = {0};  // Circular buffer of frame intervals
    milliseconds_t lastFrameTime = 0;               // Last frame timestamp
    size_t frameIndex = 0;                          // Current position in buffer

#ifdef UNIT_TEST
    // Test-only access so native unit tests can fill a synthetic sample
    // buffer directly instead of depending on real elapsed millis() between
    // tick() calls (which would make fps() tests either flaky or slow).
    friend class PerformanceMonitorTestAccess;
#endif
};
