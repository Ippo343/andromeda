#pragma once

// 1-D heat equation on the LED loop: temperature diffuses along the strip and
// cools toward zero (Newton cooling). Energy is added by slow "flames" rather
// than sparks: each flame is a long-lived (a few seconds) emitter that pours a
// modest amount of heat into its cell every frame, swelling in and fading out
// over its lifetime - so the strip glows and breathes instead of popping. On the
// L10 the square frame carries embers that kindle, bloom and die back around the
// perimeter. One RingFieldEffect channel, so it runs per-strip on any model.

#include <math.h>

#include <vector>

#include "effects/ring-field-effect.h"
#include "physics/physics-random.h"
#include "utils.h"

class HeatDiffusionRing : public RingFieldEffect
{
   public:
    HeatDiffusionRing() : RingFieldEffect(1)
    {
        CRGBPalette16 heat = HeatColors_p;
        UpscalePalette(heat, palette_);
        flames_.resize(GEOMETRY.getNumStrips());
    }

    const char* GetName() override { return "Heat Diffusion Ring"; }

   protected:
    // A slow heat emitter: sits at `pos`, lives for `life` seconds, has burned
    // for `age` seconds, and injects up to `power` heat-units per second (shaped
    // by a raised-sine envelope so it fades in and out rather than popping).
    struct Flame
    {
        uint16_t pos;
        float age;
        float life;
        float power;
    };

    // Diffusion coefficient, in (LED index)^2 per second. Fairly low thermal
    // conductivity: a flame builds a distinct localised peak rather than a broad
    // wash, but heat still visibly creeps a few cells out from it. Well under the
    // explicit-Euler stability limit (alpha * dt < 0.5 at unit grid spacing) for
    // the 16 ms substep. Needs a visual pass.
    RandParam<uint8_t, 3, 7> alpha_;
    // Heat lost per second as a fraction of the current value, /100 (0.30 ..
    // 0.65): enough that a strip fades once its flames die out, but slow enough
    // that a live flame can drive its cell close to white.
    RandParam<uint8_t, 30, 65> coolingPct_;
    // Expected new flames per second per strip, /100 (0.12 .. 0.40) - a handful
    // alight at once at most.
    RandParam<uint8_t, 12, 40> spawnRateCentiHz_;
    // Flame lifetime bounds, in tenths of a second (2.0 .. 6.5 s).
    RandParam<uint8_t, 20, 35> lifeMinDeci_;
    RandParam<uint8_t, 45, 65> lifeMaxDeci_;
    // Peak injection power (heat-units/second) per flame class. Most flames are
    // "red" and settle a cell around a warm red; "yellow" ones push it near the
    // top of the ramp; the rare "white" one pins the cell at full white for much
    // of its life. Each flame jitters +/-15% off its class value.
    RandParam<uint16_t, 110, 165> powerRed_;
    RandParam<uint16_t, 250, 330> powerYellow_;
    RandParam<uint16_t, 540, 760> powerWhite_;
    // How often a new flame is a yellow / white one, in percent (the rest are red).
    RandParam<uint8_t, 12, 24> yellowPct_;
    RandParam<uint8_t, 3, 7> whitePct_;

    static constexpr size_t MAX_FLAMES_PER_STRIP = 4;

    void seedField(size_t strip) override
    {
        // Start with one flame already burning so the first seconds aren't dark.
        spawnFlame(strip);
    }

    void injectStrip(size_t strip, milliseconds_t t, milliseconds_t dt) override
    {
        if (flames_[strip].size() >= MAX_FLAMES_PER_STRIP) return;
        if (rollEvent(spawnRateCentiHz_ / 100.0f, dt)) spawnFlame(strip);
    }

    void stepStrip(size_t strip, float dtSeconds) override
    {
        auto& T = channel(strip, 0);
        size_t n = T.size();

        // 1. Slow energy injection from the flames, then age / retire them.
        auto& flames = flames_[strip];
        for (size_t f = 0; f < flames.size();)
        {
            Flame& fl = flames[f];
            fl.age += dtSeconds;
            if (fl.age >= fl.life)
            {
                flames[f] = flames.back();
                flames.pop_back();
                continue;
            }
            // Raised-sine envelope: 0 at birth, 1 at mid-life, 0 at death.
            float env = sinf(PI * (fl.age / fl.life));
            float add = fl.power * env * dtSeconds;
            size_t c = fl.pos % n;
            size_t lo = (c == 0) ? n - 1 : c - 1;
            size_t hi = (c + 1 == n) ? 0 : c + 1;
            T[c] += add * 0.6f;
            T[lo] += add * 0.2f;
            T[hi] += add * 0.2f;
            if (T[c] > 255.0f) T[c] = 255.0f;
            if (T[lo] > 255.0f) T[lo] = 255.0f;
            if (T[hi] > 255.0f) T[hi] = 255.0f;
            f++;
        }

        // 2. Diffuse + Newton-cool.
        scratch_.assign(n, 0.0f);
        float a = (float)alpha_;
        float cool = coolingPct_ / 100.0f;
        for (size_t i = 0; i < n; i++)
        {
            float v = T[i] + a * laplacian(T, i) * dtSeconds;
            v -= cool * v * dtSeconds;
            scratch_[i] = v < 0.0f ? 0.0f : v;
        }
        T.swap(scratch_);
    }

    uint8_t colorIndex(size_t strip, size_t led) const override
    {
        float v = channel(strip, 0)[led];
        return (uint8_t)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    }

   private:
    void spawnFlame(size_t strip)
    {
        float lifeMin = lifeMinDeci_ / 10.0f;
        float lifeMax = lifeMaxDeci_ / 10.0f;

        int roll = (int)random(100);
        float base;
        if (roll < (int)(uint8_t)whitePct_)
            base = (float)(uint16_t)powerWhite_;
        else if (roll < (int)(uint8_t)whitePct_ + (int)(uint8_t)yellowPct_)
            base = (float)(uint16_t)powerYellow_;
        else
            base = (float)(uint16_t)powerRed_;

        Flame fl;
        fl.pos = (uint16_t)random(stripLen(strip));
        fl.age = 0.0f;
        fl.life = randomFloat(lifeMin, lifeMax);
        fl.power = base * randomFloat(0.85f, 1.15f);
        flames_[strip].push_back(fl);
    }

    std::vector<float> scratch_;
    std::vector<std::vector<Flame>> flames_;
};
