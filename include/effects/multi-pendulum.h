#pragma once

// Each emitter is one mass on an N-link rigid pendulum chain (physics/verlet-chain.h),
// randomly configured and simulated frictionlessly.

#include "control-hints.h"
#include "effects-utils.h"
#include "effects/emitter-field-effect.h"
#include "geometry/geometry.h"
#include "physics/verlet-chain.h"
#include "utils.h"

class MultiPendulum : public EmitterFieldEffect
{
   public:
    const char* GetName() override { return "Multi Pendulum"; }

    VerletChain chain;

    static constexpr float MIN_MASS = 1.0f;
    static constexpr float MAX_MASS = 4.0f;

    // Per-emitter glow scale-down across the 2..6 emitter range (see
    // EmitterFieldEffect::scaleGlowByEmitterCount). Like BezierSwarm,
    // MultiPendulum was left on the raw defaultBrightnessFactor; its emitters
    // cluster near the pivot so the flat-flooding is less obvious, but the same
    // shrink applies (#112). First-pass fractions - want tuning on real L10
    // hardware.
    static constexpr int MIN_EMITTERS = 2;
    static constexpr int MAX_EMITTERS = 6;
    static constexpr float GLOW_AT_MIN_EMITTERS = 0.40f;
    static constexpr float GLOW_AT_MAX_EMITTERS = 0.15f;

    // A chain needs at least 2 links to be meaningfully "multi".
    MultiPendulum() : EmitterFieldEffect(RandParam<int, 2, 6>())
    {
        controlHints = ControlHints::ROTATE_SPACE;
        scaleGlowByEmitterCount(MIN_EMITTERS, MAX_EMITTERS, GLOW_AT_MIN_EMITTERS,
                                GLOW_AT_MAX_EMITTERS);
        colors = randomComplementaryColors((int)positions.size());

        // Use the narrower half-dimension, not getScreenRadius() (== the *wider* one):
        // the chain radiates outward in arbitrary directions from the center, so on a
        // non-square board sizing off the wider dimension would let it swing outside
        // the strip's shorter axis entirely.
        float radius = (float)min(GEOMETRY.getScreenHalfWidth(), GEOMETRY.getScreenHalfHeight());
        float avgLink = (radius * 0.9f) / (float)positions.size();
        chain.initRandom(positions.size(), Vec2f(0, 0), avgLink * 0.6f, avgLink * 1.0f, MIN_MASS,
                         MAX_MASS);
        syncPositions([this](size_t i) { return chain.curr[i]; });
    }

    void updatePositions(milliseconds_t, milliseconds_t dt) override
    {
        chain.step(dt);
        syncPositions([this](size_t i) { return chain.curr[i]; });
    }
};
