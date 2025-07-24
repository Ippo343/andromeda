#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <ArduinoLog.h>

#include "utils.h"
#include "energy-param.h"

// Simple performance monitor.
// Currently it only counts the frames per second:
// just tick() it once per frame and call stat() when you want the stats.
class PerformanceMonitor
{
  private:
    milliseconds start  = 0;
    milliseconds end    = 0;
    int frames = 0;

  public:
    inline void tick() { frames++; }

    void stat()
    {
      end = millis();

      float seconds = (float)(end - start) / 1000.0;
      float fps = frames / seconds;

      Log.noticeln("FPS: %F (%d frames in %F seconds). Energy: %d", fps, frames, seconds, Energy::get());

      frames = 0;
      start = millis();
    }
};

#endif