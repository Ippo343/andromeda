#pragma once

// Shared base for the "1-D scalar field on the LED strip" family of effects
// (HeatDiffusionRing, StandingWaveRing, JellyFrame).
//
// Each strip runs its own independent field, indexed by the LED's strip-local
// index, with a PERIODIC boundary: cell 0's left neighbour is cell n-1. On the
// single-strip L10 devices this is exactly the closed perimeter loop the effects
// are designed around; on a multi-strip board (Andromeda, L70) every strip just
// evolves its own loop, with no cross-strip coupling and no assumption that a
// strip is physically a ring.
//
// Subclasses supply the update rule (stepStrip), the field -> palette-index
// mapping (colorIndex), and optionally initial conditions (seedField) and
// stochastic forcing (injectStrip). The base owns: per-strip float storage for
// up to 2 channels, the periodic Laplacian, substepped time integration via
// physics::steppedSimulate (frame-hitch protection, frame-rate independence),
// and the palette-lookup render path.

#include <array>
#include <vector>

#include "effects-base.h"
#include "effects-utils.h"
#include "physics/frame-clock.h"
#include "physics/substepper.h"
#include "utils.h"

class RingFieldEffect : public AbstractEffect
{
   public:
    // channels: how many scalar fields per LED (1 or 2). e.g. heat needs 1
    // (temperature); a wave needs 2 (displacement + velocity).
    explicit RingFieldEffect(uint8_t channels) : channels_(channels)
    {
        ch_.resize(GEOMETRY.getNumStrips());
        FOR_EACH_STRIP
        {
            size_t n = GEOMETRY.getStrip(iStrip).num_leds;
            for (uint8_t c = 0; c < channels_; c++) ch_[iStrip][c].assign(n, 0.0f);
        }

        CRGBPalette16 p16 = randomPredefinedPalette();
        UpscalePalette(p16, palette_);
    }

    void precompute(milliseconds_t t) override
    {
        // seedField() is virtual, so it can't run from the base constructor
        // (vtable isn't the derived type's yet) - do it lazily on the first frame.
        if (!seeded_)
        {
            FOR_EACH_STRIP
            {
                if (steppable(iStrip)) seedField(iStrip);
            }
            seeded_ = true;
        }

        milliseconds_t dt = clock_.tick(t);

        // Stochastic forcing is a once-per-frame event (a Bernoulli roll keyed to
        // the frame dt), not a per-substep one - run it before the integrator.
        FOR_EACH_STRIP
        {
            if (steppable(iStrip)) injectStrip(iStrip, t, dt);
        }

        physics::steppedSimulate(dt, physics::DEFAULT_MAX_SUBSTEP_MS,
                                 physics::DEFAULT_MAX_TOTAL_STEP_MS,
                                 [this](float dtSeconds)
                                 {
                                     FOR_EACH_STRIP
                                     {
                                         if (steppable(iStrip)) stepStrip(iStrip, dtSeconds);
                                     }
                                 });
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        return ColorFromPalette(palette_, colorIndex(strip->idx, led_idx));
    }

   protected:
    // ---- subclass hooks -----------------------------------------------------

    // Advance strip `strip`'s field by dtSeconds. Guaranteed num_leds >= 3.
    virtual void stepStrip(size_t strip, float dtSeconds) = 0;

    // Map the current field at (strip, led) to a 0-255 palette index.
    virtual uint8_t colorIndex(size_t strip, size_t led) const = 0;

    // Optional: set initial conditions for one strip. No-op by default (fields
    // start all-zero).
    virtual void seedField(size_t strip) {}

    // Optional: apply stochastic / periodic forcing to one strip once per frame.
    virtual void injectStrip(size_t strip, milliseconds_t t, milliseconds_t dt) {}

    // ---- helpers for subclasses ------------------------------------------------

    std::vector<float>& channel(size_t strip, uint8_t c) { return ch_[strip][c]; }
    const std::vector<float>& channel(size_t strip, uint8_t c) const { return ch_[strip][c]; }

    size_t stripLen(size_t strip) const { return ch_[strip][0].size(); }

    // Discrete Laplacian with periodic (wrap-around) boundary: f[i-1] - 2 f[i] + f[i+1].
    static float laplacian(const std::vector<float>& f, size_t i)
    {
        size_t n = f.size();
        const float& left = f[(i == 0) ? n - 1 : i - 1];
        const float& right = f[(i + 1 == n) ? 0 : i + 1];
        return left - 2.0f * f[i] + right;
    }

    // Shared leapfrog integrator for the two "damped wave on a ring" effects
    // (StandingWaveRing, JellyFrame): channel 0 is displacement (u), channel 1 is
    // velocity (v). v += (c2*laplacian(u) - kTether*u) * dt, damped, then
    // u += v*dt - symplectic (v uses the old u, u then uses the new v) so no
    // scratch buffer is needed. kTether = 0 recovers the plain wave equation
    // (StandingWaveRing); JellyFrame passes a nonzero tether to pull the field
    // back toward rest between kicks.
    void dampedWaveStep(size_t strip, float dtSeconds, float c2, float kTether, float damp)
    {
        auto& u = channel(strip, 0);
        auto& v = channel(strip, 1);
        size_t n = u.size();
        for (size_t i = 0; i < n; i++)
            v[i] = (v[i] + (c2 * laplacian(u, i) - kTether * u[i]) * dtSeconds) * damp;
        for (size_t i = 0; i < n; i++) u[i] += v[i] * dtSeconds;
    }

    CRGBPalette256 palette_;

#ifdef UNIT_TEST
   public:
    // Test-only: direct field access so native tests can plant a known state and
    // assert on the subclass's update rule without going through the random
    // injector or the lazy seed.
    std::vector<float>& fieldForTest(size_t strip, uint8_t c) { return ch_[strip][c]; }
#endif

   private:
    // A periodic Laplacian needs at least 3 distinct cells; strips shorter than
    // that (e.g. Andromeda's short centre strip) are left inert rather than
    // special-cased everywhere.
    bool steppable(size_t strip) const { return ch_[strip][0].size() >= 3; }

    uint8_t channels_;
    std::vector<std::array<std::vector<float>, 2>> ch_;  // ch_[strip][channel][led]
    FrameClock clock_;
    bool seeded_ = false;
};
