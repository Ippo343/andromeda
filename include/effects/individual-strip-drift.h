#pragma once

#include <vector>

#include "effects-utils.h"
#include "effects/per-strip-color-effect.h"
#include "energy-param.h"
#include "utils.h"

using std::vector;

// Each strip slowly drifts to a new random color independently
class IndividualStripDrift : public PerStripColorEffect
{
   public:
    virtual const char* GetName() { return "Individual Strip Drift"; }

    // Random range for the transition duration
    EnergyParam<milliseconds_t, 2 SECONDS, 250> transitionDurationMin;
    EnergyParam<milliseconds_t, 20 SECONDS, 5 SECONDS> transitionDurationMax;

    // Per-strip timing info
    vector<milliseconds_t> transitionStartTimes;
    vector<milliseconds_t> transitionEndTimes;

    // Per-strip color info (colors holds the current interpolated color - see
    // PerStripColorEffect)
    vector<CRGB> prevColors;
    vector<CRGB> targetColors;

    IndividualStripDrift()
        : prevColors(GEOMETRY.getNumStrips(), CRGB::Black),
          targetColors(GEOMETRY.getNumStrips(), CRGB::Black),
          transitionStartTimes(GEOMETRY.getNumStrips(), 0),
          transitionEndTimes(GEOMETRY.getNumStrips(), 0)
    {
        // Initialize all strips to a random color and set up the first transition
        milliseconds_t now = millis();
        FOR_EACH_STRIP
        {
            targetColors[iStrip] = randomColor();
            milliseconds_t duration = random(transitionDurationMin, transitionDurationMax);
            transitionStartTimes[iStrip] = now;
            transitionEndTimes[iStrip] = now + duration;
        }
    }

    void precompute(milliseconds_t t) override
    {
        FOR_EACH_STRIP
        {
            // Rollover-safe: compare elapsed-since-start against the stored
            // duration (itself an unsigned delta), never t against the
            // absolute end stamp. `t >= transitionEndTimes[i]` flips at the
            // millis() wrap - the end stamp was computed by addition before
            // the wrap - and re-rolls every strip's colour on every frame.
            milliseconds_t elapsed = t - transitionStartTimes[iStrip];
            milliseconds_t duration = transitionEndTimes[iStrip] - transitionStartTimes[iStrip];

            if (elapsed >= duration)
            {
                prevColors[iStrip] = targetColors[iStrip];
                targetColors[iStrip] = randomColor();
                transitionStartTimes[iStrip] = t;
                duration = random(transitionDurationMin, transitionDurationMax);
                transitionEndTimes[iStrip] = t + duration;
                elapsed = 0;
            }

            // Compute the interpolation factor between the two colors
            uint8_t factor = cmap(elapsed, 0, duration, 0, 255);
            factor = ease8InOutCubic(factor);

            colors[iStrip] = CRGB::blend(prevColors[iStrip], targetColors[iStrip], factor);
        }
    }
};
