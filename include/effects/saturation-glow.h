#pragma once

#include <vector>

#include "effects-base.h"
#include "utils.h"

using std::vector;

// Whole mirror moodlight pulsating around a central saturation
class SaturationGlow : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "Saturation Glow"; }

    // Time scale factor for the perlin random noise
    RandParam<unsigned short, 500, 5000> hueTimeScale;

    RandParam<uint8_t, 100, 156>
        saturationCenter;  // skewed towards high saturation because colors are pretty
    uint8_t saturationAmplitude;

    // Each strip has a random cycle time
    vector<RandParam<milliseconds_t, (1 MINUTES), (4 MINUTES)>> cycleTime;

    uint8_t hue;         // current hue (same for all strips)
    vector<CRGB> color;  // specific color per strip

    SaturationGlow()
        : cycleTime(GEOMETRY.getNumStrips()),  // this SHOULD call the default constructor of
                                               // RandParam, picking 7 random values. I think.
          color(GEOMETRY.getNumStrips())
    {
        saturationAmplitude = min(static_cast<uint8_t>(saturationCenter),
                                  static_cast<uint8_t>(255 - saturationCenter));
    }

    void precompute(milliseconds_t t) override
    {
        hue = inoise8(t / hueTimeScale);

        FOR_EACH_STRIP
        {
            long scaledWave =
                scaledCubicWave8(t, cycleTime[iStrip], -saturationAmplitude, saturationAmplitude);
            uint8_t sat = constrain(saturationCenter + scaledWave, 0, 255);
            color[iStrip] = CHSV(hue, sat, 255);
        }
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        return color[strip->idx];
    }
};
