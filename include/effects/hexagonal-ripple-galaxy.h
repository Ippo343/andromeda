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

    RandParam<uint8_t, 1, 3> hueHarmonic;     // Hue cycles per revolution - must be an
                                              // integer so the hue closes on itself (#67)
    RandParam<uint8_t, 3, 5> hueRadiusShift;  // Hue radius contribution (>> 3 to >> 5)

   public:
    const char* GetName() override { return "Hexagonal Ripple Galaxy"; }

    // Wrap-safe angular hue term (#67). Periodic in angle8 with period 256, so
    // the value at angle8 == 255 is adjacent in hue space to the value at
    // angle8 == 0 - no seam along the theta-origin ray. The previous mapping
    // (angle8 >> hueAngleShift) truncated the span instead of wrapping it,
    // turning that wrap into a jump of up to half the color wheel.
    static uint8_t angularHue(uint8_t angle8, uint8_t harmonic)
    {
        return static_cast<uint8_t>(angle8 * harmonic);
    }

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

        // Use FastLED's built-in HSV to RGB conversion
        return CHSV(computeHue(radius8, angle8), 255, 255);  // Maximum saturation and brightness
    }

#ifdef UNIT_TEST
   public:
    // Test-only: exposes the hue actually assigned to a LED so native tests
    // can assert on hue-space continuity across the theta-origin ray (#67)
    // directly, rather than through CHSV(hue,255,255)'s RGB conversion - whose
    // per-channel slope near a rainbow segment boundary can turn even a small,
    // wrap-safe hue step into a large RGB delta and make a threshold on RGB
    // unreliable.
    uint8_t computeHueForTest(uint8_t radius8, uint8_t angle8)
    {
        return computeHue(radius8, angle8);
    }
#endif

   private:
    // Create hue with randomized hue distribution
    uint8_t computeHue(uint8_t radius8, uint8_t angle8)
    {
        return baseHue + angularHue(angle8, static_cast<uint8_t>(hueHarmonic)) +
               (radius8 >> static_cast<uint8_t>(hueRadiusShift));
    }

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
