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
// convert to milliamps at the model's actual rail voltage (ModelConfig::rail_millivolts
// - see src/main.cpp's FastLED.setMaxPowerInMilliWatts call, which budgets against
// the same rail). uint64_t intermediates avoid overflow: unscaledMilliwattsAt255 can
// already approach UINT32_MAX's headroom on a large panel at full white, and scaling
// by 1000 before the final divide (to keep the mV-denominated division exact rather
// than losing precision to an early integer divide-by-volts) would overflow a 32-bit
// intermediate on its own.
inline uint32_t estimateCurrentMa(uint32_t unscaledMilliwattsAt255, uint8_t appliedBrightness,
                                  uint16_t railMillivolts = 5000)
{
    uint64_t scaledMilliwatts = (uint64_t)unscaledMilliwattsAt255 * appliedBrightness / 255;
    return (uint32_t)(scaledMilliwatts * 1000 / railMillivolts);
}

// Per-frame draw swings wildly with content (a single white pixel vs. a full
// strip of it), so a raw instantaneous reading is too noisy to read on the
// Advanced page. Smooth it the same way PerformanceMonitor smooths fps(): a
// fixed circular buffer of recent samples, averaged on read.
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
        samples[sampleIndex] = estimateCurrentMa(unscaledMw, FastLED.getBrightness(),
                                                 GEOMETRY.getConfig()->rail_millivolts);
        sampleIndex = (sampleIndex + 1) & INDEX_MASK;  // Fast power-of-2 modulo
    }

    // Deliberately always averages over the full SAMPLE_COUNT, including any
    // still-zero slots at boot - same cheap running-average trade-off as
    // PerformanceMonitor::fps() (see perf-monitor.h).
    inline uint32_t currentMa() const
    {
        uint64_t total = 0;
        for (size_t i = 0; i < SAMPLE_COUNT; i++) total += samples[i];
        return static_cast<uint32_t>(total / SAMPLE_COUNT);
    }

   private:
    PowerMonitor() = default;

    static constexpr size_t SAMPLE_COUNT = 128;
    static constexpr size_t INDEX_MASK = SAMPLE_COUNT - 1;

    uint32_t samples[SAMPLE_COUNT] = {0};  // Circular buffer of recent mA estimates
    size_t sampleIndex = 0;

#ifdef UNIT_TEST
    // Test-only access so native unit tests can fill a synthetic sample
    // buffer directly instead of depending on GEOMETRY/FastLED state.
    friend class PowerMonitorTestAccess;
#endif
};
