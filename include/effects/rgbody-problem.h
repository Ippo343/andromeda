#pragma once

// The real RGB body problem: N masses, each with its own color, mass, and initial
// position/velocity, under mutual Newtonian gravity (physics/nbody-system.h).

#include "control-hints.h"
#include "effects-utils.h"
#include "effects/emitter-field-effect.h"
#include "geometry/geometry.h"
#include "physics/nbody-system.h"
#include "utils.h"

class RGBodyProblem : public EmitterFieldEffect
{
   public:
    const char* GetName() override { return "RG Body Problem"; }

    NBodySystem sim;

    static constexpr float MIN_MASS = 1.0f;
    static constexpr float MAX_MASS = 5.0f;
    static constexpr float MIN_SPEED_MM_S = 2.0f;
    static constexpr float MAX_SPEED_MM_S = 8.0f;

    // A genuine N-body problem needs at least 3 masses.
    static constexpr int MIN_BODIES = 3;
    static constexpr int MAX_BODIES = 6;
    static constexpr float BRIGHTNESS_FACTOR_AT_MIN_BODIES = 11.0f / 32.0f;
    static constexpr float BRIGHTNESS_FACTOR_AT_MAX_BODIES = 1.0f / 8.0f;

    RGBodyProblem() : EmitterFieldEffect(RandParam<int, MIN_BODIES, MAX_BODIES>())
    {
        controlHints = ControlHints::ROTATE_SPACE;
        colors = randomComplementaryColors((int)positions.size());
        // More bodies means more simultaneous light sources, so shrink each one (smaller
        // brightness falloff radius) as N grows, from the size tuned for 3 bodies down to
        // the smallest size tried (at 6 bodies), so the scene doesn't get overly bright/busy.
        scaleGlowByEmitterCount(MIN_BODIES, MAX_BODIES, BRIGHTNESS_FACTOR_AT_MIN_BODIES,
                                BRIGHTNESS_FACTOR_AT_MAX_BODIES);

        // Use the narrower half-dimension, not getScreenRadius() (== the *wider* one):
        // NBodySystem places/bounds bodies in a full circle of this radius, so on a
        // non-square board the wider dimension would let bodies wander outside the
        // strip's shorter axis entirely.
        float boundsRadius =
            (float)min(GEOMETRY.getScreenHalfWidth(), GEOMETRY.getScreenHalfHeight());
        sim.initRandom(positions.size(), boundsRadius, MIN_MASS, MAX_MASS, MIN_SPEED_MM_S,
                       MAX_SPEED_MM_S);
        syncPositions([this](size_t i) { return sim.pos[i]; });
    }

    void updatePositions(milliseconds_t, milliseconds_t dt) override
    {
        sim.step(dt);
        syncPositions([this](size_t i) { return sim.pos[i]; });
    }
};
