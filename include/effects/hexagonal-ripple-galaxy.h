#pragma once

#include <vector>

#include "effects-base.h"
#include "utils.h"

// Dreamed up by Claude!
// Optimized for Arduino with FastLED
class HexagonalRippleGalaxy : public AbstractEffect
{
   private:
    // Cached values computed once per frame
    uint8_t timeScale8;     // Time scaled to 0-255
    uint8_t spiralOffset8;  // Spiral offset as 0-255
    uint8_t baseHue;

    // Randomizable parameters for variation
    RandParam<uint8_t, 3, 5> timeShift;    // Time scale divisor (>> 3 to >> 5) - faster
    RandParam<uint8_t, 5, 7> spiralShift;  // Spiral rotation divisor (>> 5 to >> 7) - faster
    RandParam<uint8_t, 6, 9> hueShift;     // Hue cycling divisor (>> 6 to >> 9) - faster

    RandParam<uint8_t, 1, 3> ripple1Freq;       // Ripple 1 frequency multiplier
    RandParam<uint8_t, 1, 4> ripple1TimeScale;  // Ripple 1 time multiplier
    RandParam<uint8_t, 0, 2> ripple2Freq;       // Ripple 2 frequency multiplier (0 = off)
    RandParam<uint8_t, 1, 3> ripple2TimeScale;  // Ripple 2 time multiplier

    RandParam<uint8_t, 2, 5> spiralCount;        // Number of spiral arms
    RandParam<uint8_t, 2, 4> spiralRadiusShift;  // Spiral-radius coupling (>> 2 to >> 4)

    RandParam<uint8_t, 0, 2> hueAngleShift;   // Hue angle contribution (>> 0 to >> 2)
    RandParam<uint8_t, 3, 5> hueRadiusShift;  // Hue radius contribution (>> 3 to >> 5)

   public:
    const char* GetName() override { return "Hexagonal Ripple Galaxy"; }

    void precompute(milliseconds_t t) override
    {
        // Scale time to 8-bit for FastLED trig functions
        // Use randomized time scaling
        timeScale8 = t >> static_cast<uint8_t>(timeShift);

        // Spiral rotation with randomized speed
        spiralOffset8 = t >> static_cast<uint8_t>(spiralShift);

        // Hue cycling with randomized period
        baseHue = t >> static_cast<uint8_t>(hueShift);

        ensurePerLedCache();
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        // radius8/angle8 are pure functions of a LED's fixed polar coordinates
        // - no time dependency - so they're precomputed once instead of
        // remapped every LED every frame (see ensurePerLedCache()).
        uint8_t radius8 = radius8Cache[strip->idx][led_idx];
        uint8_t angle8 = angle8Cache[strip->idx][led_idx];

        // Create ripples with randomized parameters
        uint8_t ripple1 = sin8((radius8 * static_cast<uint8_t>(ripple1Freq)) -
                               (timeScale8 * static_cast<uint8_t>(ripple1TimeScale)));
        uint8_t ripple2 = sin8((radius8 * static_cast<uint8_t>(ripple2Freq)) -
                               (timeScale8 * static_cast<uint8_t>(ripple2TimeScale)));

        // Combine ripples
        uint8_t rippleSum = ((ripple1 >> 1) + (ripple2 >> 2)) + 64;

        // Add spiral component with randomized parameters
        uint8_t spiral = sin8((angle8 * static_cast<uint8_t>(spiralCount)) + spiralOffset8 +
                              (radius8 >> static_cast<uint8_t>(spiralRadiusShift)));

        // Combine ripples and spiral for saturation modulation instead of brightness
        uint8_t saturationMod = ((rippleSum >> 1) + (spiral >> 1));

        // Create color with randomized hue distribution
        uint8_t hue = baseHue + (angle8 >> static_cast<uint8_t>(hueAngleShift)) +
                      (radius8 >> static_cast<uint8_t>(hueRadiusShift));

        // Use FastLED's built-in HSV to RGB conversion
        return CHSV(hue, 255, 255);  // Maximum saturation and brightness
    }

   private:
    std::vector<std::vector<uint8_t>> radius8Cache;
    std::vector<std::vector<uint8_t>> angle8Cache;
    bool perLedCacheReady = false;

    // Lazy, not constructor-time: this effect has no ROTATE_SPACE hint today,
    // but building the cache on first precompute() rather than construction
    // keeps the pattern safe regardless (see ninja-star.h for the ROTATE_SPACE
    // case this matters for).
    void ensurePerLedCache()
    {
        if (perLedCacheReady) return;

        radius8Cache.resize(GEOMETRY.getNumStrips());
        angle8Cache.resize(GEOMETRY.getNumStrips());

        FOR_EACH_STRIP
        {
            LedStrip& strip = GEOMETRY.getStrip(iStrip);
            radius8Cache[iStrip].resize(strip.num_leds);
            angle8Cache[iStrip].resize(strip.num_leds);

            FOR_EACH_LED(iStrip)
            {
                Led& led = strip.leds[iLed];
                radius8Cache[iStrip][iLed] =
                    map(led.polar.radius, 0, GEOMETRY.getScreenRadius(), 0, 255);
                angle8Cache[iStrip][iLed] =
                    map(led.polar.cdegrees % FULL_CIRCLE, 0, FULL_CIRCLE, 0, 255);
            }
        }

        perLedCacheReady = true;
    }
};
