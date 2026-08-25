#pragma once

#include <vector>

#include "effects-base.h"
#include "effects-utils.h"
#include "moodlight.h"
#include "utils.h"

using std::vector;

// Misnomer as it's not actually solving the 3 body problem... yet.
// Right now it only has 3 emitters moving independently via sine waves.
// The color of each LED is decided based on the distance from each emitter.
// It also has a single-channel mode where there is a single emitter
// hooked up to a moodlight source.
// TODO: use both 3 body problem and double pendulum
// TODO: render in the correct bounding box
class RGBodyProblem : public AbstractEffect
{
   public:
    virtual const char* GetName() { return "RGBodyProblem"; }

    RandParam<int, 1, 3> emittersCount;
    RandBool fixedColors;

    vector<CartesianCoordinates> locations;
    vector<RandSine<1, 20>> sx;
    vector<RandSine<1, 20>> sy;
    vector<CHSV> colors;

    MoodLight moodlight;

    RGBodyProblem()
        : locations(emittersCount), colors(emittersCount), sx(emittersCount), sy(emittersCount)
    {
        controlHints = ControlHints::ROTATE_SPACE;

        for (size_t i = 0; i < emittersCount; i++)
        {
            locations[i] = CartesianCoordinates();
            sx[i] = RandSine<1, 20>();
            sy[i] = RandSine<1, 20>();
        }

        // If there's only one emitter, it will use a moodlight instead
        colors = randomComplementaryColors(emittersCount);
    }

    // Helper to scale the result of a sine wave to the screen size
    short scale(int v)
    {
        return map(v, 0, 255, -GEOMETRY.getScreenRadius(), GEOMETRY.getScreenRadius());
    }

    void precompute(milliseconds_t t) override
    {
        for (size_t i = 0; i < emittersCount; i++)
        {
            locations[i].x = scale(sx[i].evaluate(0));
            locations[i].y = scale(sy[i].evaluate(0));
        }
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        if (emittersCount == 1)
        {
            CRGB rawColor = moodlight.evaluate();
            uint8_t v = brightnessFromEmitter(led, locations[0]);
            rawColor.nscale8(v);
            return rawColor;
        }

        CRGB finalColor = CRGB::Black;
        for (size_t i = 0; i < emittersCount; i++)
        {
            CRGB emitterColor = colors[i];
            uint8_t v = brightnessFromEmitter(led, locations[i]);
            emitterColor.nscale8(v);
            finalColor += emitterColor;
        }

        return finalColor;
    }
};
