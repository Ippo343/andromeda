#pragma once

// The 1-D wave equation on the LED loop: u_tt = c^2 u_xx, integrated with a
// symplectic (semi-implicit Euler) leapfrog, lightly damped, and plucked at
// random every few seconds. A watchdog force-plucks any strip that has rung all
// the way down, so it can never lock to a flat (uniform-colour) rest state. On
// the L10 the square frame carries travelling and standing waves that reflect
// around the perimeter. Two RingFieldEffect channels (displacement, velocity),
// so it runs per-strip on any model.

#include <math.h>

#include <vector>

#include "effects/ring-field-effect.h"
#include "utils.h"

class StandingWaveRing : public RingFieldEffect
{
   public:
    StandingWaveRing() : RingFieldEffect(2) {}

    const char* GetName() override { return "Standing Wave Ring"; }

   protected:
    static constexpr uint8_t U = 0;  // displacement
    static constexpr uint8_t V = 1;  // velocity

    // Wave "stiffness" c^2, in (LED index)^2 / s^2. The leapfrog is stable while
    // c2 * dt^2 * 4 <= 4 at unit grid spacing; dt <= 16 ms here leaves plenty of
    // margin at this range. Needs a visual pass on hardware.
    RandParam<uint16_t, 250, 800> stiffness_;
    // Energy lost per second as a fraction of velocity, /1000 (waves should ring
    // for a good while before fading).
    RandParam<uint8_t, 4, 30> dampPerMille_;
    // Expected plucks per second, /100.
    RandParam<uint8_t, 20, 70> pluckRateCentiHz_;
    // Displacement amplitude of a pluck.
    RandParam<uint8_t, 90, 170> pluckAmplitude_;
    // Raised-cosine half-width of a pluck, in tenths of a cell (1.6 .. 3.0).
    RandParam<uint8_t, 16, 30> pluckWidthDeci_;
    // Mean |displacement| per cell below which the strip counts as "rung down"
    // and gets a fresh pluck regardless of the random roll - at this level the
    // whole ring is within ~2 palette steps of the midpoint, i.e. visually flat.
    static constexpr float DEAD_LEVEL_PER_CELL = 2.0f;

    void seedField(size_t strip) override { pluck(strip, random(stripLen(strip))); }

    void injectStrip(size_t strip, milliseconds_t t, milliseconds_t dt) override
    {
        // Watchdog: never let a strip settle to a flat, uniform-colour rest state.
        const auto& u = channel(strip, U);
        float activity = 0.0f;
        for (float x : u) activity += fabsf(x);
        if (activity < DEAD_LEVEL_PER_CELL * (float)u.size())
        {
            pluck(strip, random(stripLen(strip)));
            return;
        }

        if (rollEvent(pluckRateCentiHz_ / 100.0f, dt)) pluck(strip, random(stripLen(strip)));
    }

    void stepStrip(size_t strip, float dtSeconds) override
    {
        float c2 = (float)stiffness_;
        float damp = 1.0f - (dampPerMille_ / 1000.0f) * dtSeconds;
        dampedWaveStep(strip, dtSeconds, c2, /*kTether=*/0.0f, damp);
    }

    uint8_t colorIndex(size_t strip, size_t led) const override
    {
        // Rest maps to the palette midpoint; crest and trough diverge from it.
        float x = 128.0f + channel(strip, U)[led] * (127.0f / (float)(uint8_t)pluckAmplitude_);
        return (uint8_t)(x < 0.0f ? 0.0f : (x > 255.0f ? 255.0f : x));
    }

   private:
    void pluck(size_t strip, size_t centre)
    {
        auto& u = channel(strip, U);
        long n = (long)u.size();
        float width = pluckWidthDeci_ / 10.0f;
        float amplitude = (float)(uint8_t)pluckAmplitude_;
        long w = (long)ceilf(width);
        for (long d = -w; d <= w; d++)
        {
            float win = 0.5f + 0.5f * cosf(PI * (float)d / (width + 1.0f));
            size_t idx = (size_t)(((long)centre + d) % n + n) % n;
            u[idx] += amplitude * win;
        }
    }
};
