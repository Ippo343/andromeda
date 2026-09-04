#pragma once

#include <FastLED.h>

#include "geometry/geometry.h"

// Estimated current draw vs. the configured budget (ModelConfig::max_milliamps),
// surfaced on the Advanced page's Diagnostics card. Mirrors PerformanceMonitor:
// tick() once per rendered frame from FASTLED_SHOW(), currentMa() read
// lock-free from any task (same "eventually consistent" single-word read as
// PerformanceMonitor::fps() - see perf-monitor.h).

// Pure, natively-testable arithmetic: FastLED's calculate_unscaled_power_mW()
// returns milliwatts the buffer would draw at brightness 255; scale that down
// to the brightness actually applied this frame (FastLED.getBrightness()) and
// convert to milliamps at the fixed 5V rail every ModelConfig::max_milliamps
// budget assumes (see src/main.cpp's FastLED.setMaxPowerInVoltsAndMilliamps
// call). uint64_t intermediate avoids overflow: unscaledMilliwattsAt255 can
// already approach UINT32_MAX's headroom on a large panel at full white.
inline uint32_t estimateCurrentMa(uint32_t unscaledMilliwattsAt255, uint8_t appliedBrightness,
                                  uint8_t volts = 5)
{
    uint64_t scaledMilliwatts = (uint64_t)unscaledMilliwattsAt255 * appliedBrightness / 255;
    return (uint32_t)(scaledMilliwatts / volts);
}

class PowerMonitor
{
   public:
    static inline PowerMonitor& Instance()
    {
        static PowerMonitor instance;
        return instance;
    }

    PowerMonitor(const PowerMonitor&) = delete;
    PowerMonitor& operator=(const PowerMonitor&) = delete;

    inline void tick()
    {
        uint32_t unscaledMw = 0;
        for (size_t i = 0; i < GEOMETRY.getNumStrips(); i++)
        {
            LedStrip& strip = GEOMETRY.getStrip(i);
            unscaledMw += calculate_unscaled_power_mW(strip.buffer, strip.num_leds);
        }
        estimatedMa = estimateCurrentMa(unscaledMw, FastLED.getBrightness());
    }

    inline uint32_t currentMa() const { return estimatedMa; }

   private:
    PowerMonitor() = default;

    uint32_t estimatedMa = 0;
};
