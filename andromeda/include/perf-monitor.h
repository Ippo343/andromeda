#pragma once

#include <ArduinoLog.h>

#include "utils.h"
#include "energy-param.h"

// Simple performance monitor.
// Currently it only counts the frames per second:
// just tick() it once per frame and call stat() when you want the stats.
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

    inline void tick() { frames++; }

    float fps()
    {
      milliseconds_t end = millis();
      float seconds = (float)(end - start) / 1000.0;
      return frames / seconds;
    }

    void stat()
    {
      milliseconds_t end = millis();
      float seconds = (float)(end - start) / 1000.0;
      Log.noticeln("FPS: %F (%d frames in %F seconds). Energy: %d", fps(), frames, seconds, Energy::get());
    }

    void reset()
    {
      frames = 0;
      start = millis();
    }

  private:
    PerformanceMonitor() = default;

    milliseconds_t start  = 0;
    unsigned int frames = 0;
};