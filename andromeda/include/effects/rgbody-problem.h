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
    const char* GetName() override { return "RGBodyProblem"; }

    NBodySystem sim;

    static constexpr float MIN_MASS = 1.0f;
    static constexpr float MAX_MASS = 5.0f;
    static constexpr float MIN_SPEED_MM_S = 20.0f;
    static constexpr float MAX_SPEED_MM_S = 120.0f;

    // A genuine N-body problem needs at least 3 masses.
    RGBodyProblem() : EmitterFieldEffect(RandParam<int, 3, 8>())
    {
        controlHints = ControlHints::ROTATE_SPACE;
        colors = randomComplementaryColors((int)positions.size());

        // Use the narrower half-dimension, not getScreenRadius() (== the *wider* one):
        // NBodySystem places/bounds bodies in a full circle of this radius, so on a
        // non-square board the wider dimension would let bodies wander outside the
        // strip's shorter axis entirely.
        float boundsRadius =
            (float)min(GEOMETRY.getScreenHalfWidth(), GEOMETRY.getScreenHalfHeight());
        sim.initRandom(positions.size(), boundsRadius, MIN_MASS, MAX_MASS, MIN_SPEED_MM_S,
                       MAX_SPEED_MM_S);
        syncPositions();
    }

    void updatePositions(milliseconds_t, milliseconds_t dt) override
    {
        sim.step(dt);
        syncPositions();
    }

   private:
    void syncPositions()
    {
        for (size_t i = 0; i < positions.size(); i++) positions[i] = sim.pos[i];
    }
};
