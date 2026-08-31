#pragma once

// A damped mass-spring ring: the wave equation plus a restoring "tether" pulling
// every cell back to rest, so a disturbance rings and then settles - like the
// L10's square frame were made of jelly and got nudged. Re-excited with a shear
// impulse every few seconds so it never fully stops. Two RingFieldEffect
// channels (displacement, velocity); runs per-strip on any model.

#include <math.h>

#include <vector>

#include "effects/ring-field-effect.h"
#include "physics/physics-random.h"
#include "utils.h"

class JellyFrame : public RingFieldEffect
{
   public:
    JellyFrame() : RingFieldEffect(2)
    {
        // Full-hue sweep palette: colorIndex() encodes displacement as a hue
        // offset from a slowly drifting base.
        CHSVPalette16 hsv(CHSV(0, 255, 255), CHSV(64, 255, 255), CHSV(128, 255, 255),
                          CHSV(192, 255, 255));
        UpscalePalette(hsv, palette_);
    }

    const char* GetName() override { return "Jelly Frame"; }

    void precompute(milliseconds_t t) override
    {
        RingFieldEffect::precompute(t);
        baseHue_ = (uint8_t)(t >> 6);  // slow continuous drift
    }

   protected:
    static constexpr uint8_t U = 0;
    static constexpr uint8_t V = 1;

    RandParam<uint16_t, 200, 600> stiffness_;     // c^2, neighbour coupling
    RandParam<uint8_t, 8, 26> tetherPerCenti_;    // restoring pull toward rest, /100
    RandParam<uint8_t, 20, 60> dampPerCenti_;     // velocity loss per second, /100
    RandParam<uint8_t, 30, 70> kickRateCentiHz_;  // shear re-excitations per second, /100
    RandParam<uint8_t, 60, 120> kickSpeed_;       // shear impulse velocity amplitude
    RandParam<uint8_t, 9, 18> hueGainDeci_;       // hue units per unit displacement, /10
    static constexpr uint32_t KICK_ROLL_LIMIT = 100000;

    void seedField(size_t strip) override { shearKick(strip); }

    void injectStrip(size_t strip, milliseconds_t t, milliseconds_t dt) override
    {
        uint32_t threshold = rateToThreshold(kickRateCentiHz_ / 100.0f, dt, KICK_ROLL_LIMIT);
        if ((uint32_t)random(KICK_ROLL_LIMIT) < threshold) shearKick(strip);
    }

    void stepStrip(size_t strip, float dtSeconds) override
    {
        float c2 = (float)stiffness_;
        float kTether = tetherPerCenti_ / 100.0f;
        float damp = 1.0f - (dampPerCenti_ / 100.0f) * dtSeconds;
        dampedWaveStep(strip, dtSeconds, c2, kTether, damp);
    }

    uint8_t colorIndex(size_t strip, size_t led) const override
    {
        return (uint8_t)((float)baseHue_ + channel(strip, U)[led] * (hueGainDeci_ / 10.0f));
    }

   private:
    uint8_t baseHue_ = 0;

    // Add a sinusoidal shear to the velocity field: a low spatial mode so the
    // ring twists rather than just jittering. On a closed loop the mode number
    // must be an integer for the impulse to be continuous across the seam.
    void shearKick(size_t strip)
    {
        auto& v = channel(strip, V);
        size_t n = v.size();
        int mode = 1 + (int)random(3);  // 1..3
        float phase = randomFloat(0.0f, 2.0f * PI);
        float speed = (float)(uint8_t)kickSpeed_;
        for (size_t i = 0; i < n; i++)
            v[i] += speed * sinf(2.0f * PI * (float)mode * (float)i / (float)n + phase);
    }
};
