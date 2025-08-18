#pragma once

#include <ArduinoLog.h>
#include <FastLED.h>

// Use this macro instead of calling FastLED.show()
// So that we automatically count every frame, no matter where it's called from.
// This fixes the annoying problem that the FPS of animations could not be measured
// as only the main loop was being counted.
#define FASTLED_SHOW() do { \
    FastLED.show(); \
    PerformanceMonitor::Instance().tick(); \
} while(0)


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

    inline void tick() {
        unsigned short now = millis();

        if (lastFrameTime != 0) {
            frameTimes[frameIndex] = now - lastFrameTime;
            frameIndex = (frameIndex + 1) & INDEX_MASK;  // Fast power-of-2 modulo
        }

        lastFrameTime = now;
    }

    float fps()
    {
        unsigned short totalTime = 0;

        for (size_t i = 0; i < SAMPLE_COUNT; i++) {
            totalTime += frameTimes[i];
        }

        float avgFrameTime = totalTime / (float)SAMPLE_COUNT;
        return 1000.0f / avgFrameTime;
    }

    void stat()
    {
        Log.noticeln("FPS: %F", fps());
    }

  private:
    PerformanceMonitor() = default;

    // Power of 2 for fast modulo: did you know that using a power of 2 allows for a much faster modulo operation?
    // If N is a power of 2, then (x % N) is equivalent to (x & (N - 1)).
    // I didn't. Thanks Claude!
    static constexpr size_t SAMPLE_COUNT = 128;
    static constexpr size_t INDEX_MASK = SAMPLE_COUNT - 1;

    unsigned short frameTimes[SAMPLE_COUNT] = {0};     // Circular buffer of frame intervals
    unsigned short lastFrameTime = 0;                  // Last frame timestamp
    size_t frameIndex = 0;                             // Current position in buffer
};
