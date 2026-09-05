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
    uint8_t baseHue;

    // Randomizable parameters for variation
    RandParam<uint8_t, 6, 9> hueShift;  // Hue cycling divisor (>> 6 to >> 9) - faster

    RandParam<uint8_t, 0, 2> hueAngleShift;   // Hue angle contribution (>> 0 to >> 2)
    RandParam<uint8_t, 3, 5> hueRadiusShift;  // Hue radius contribution (>> 3 to >> 5)

   public:
    const char* GetName() override { return "Hexagonal Ripple Galaxy"; }

    void precompute(milliseconds_t t) override
    {
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
                // getMaxLedRadius(), not getScreenRadius(): see NinjaStar - the
                // outer ring of LEDs overflowed the uint8_t otherwise.
                radius8Cache[iStrip][iLed] =
                    map(led.polar.radius, 0, GEOMETRY.getMaxLedRadius(), 0, 255);
                angle8Cache[iStrip][iLed] =
                    map(led.polar.cdegrees % FULL_CIRCLE, 0, FULL_CIRCLE, 0, 255);
            }
        }

        perLedCacheReady = true;
    }
};
