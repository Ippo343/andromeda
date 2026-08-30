#pragma once

// Point masses bouncing elastically inside an axis-aligned box, at constant speed
// so the simulation is energy-stable for indefinite runtime (no damping, no
// forces - just free flight between wall reflections). Written for the
// BouncingBallGlow effect: each mass is an emitter whose inverse-square glow
// lands on the LED frame, and the box is the render area. Deliberately
// axis-aligned (not rotated) so the balls strike a square device's sides
// square-on.
//
// No ball-ball collision: overlapping glows simply add, which looks fine and
// keeps this to a handful of lines. Mirrors the initRandom()/step() shape of the
// other physics/ modules (NBodySystem, VerletChain), stepping through
// physics::steppedSimulate for frame-hitch protection.

#include <math.h>

#include <vector>

#include "physics/physics-random.h"
#include "physics/substepper.h"
#include "physics/vec2f.h"
#include "utils.h"

class BoxBounce
{
   public:
    std::vector<Vec2f> pos, vel;
    float halfW = 0, halfH = 0;  // box half-extents, origin at centre

    static constexpr milliseconds_t MAX_SUBSTEP_MS = 16;
    static constexpr milliseconds_t MAX_TOTAL_STEP_MS = 64;

    void initRandom(size_t n, float halfWidthMm, float halfHeightMm, float minSpeed, float maxSpeed)
    {
        halfW = halfWidthMm;
        halfH = halfHeightMm;
        pos.assign(n, Vec2f());
        vel.assign(n, Vec2f());

        for (size_t i = 0; i < n; i++)
        {
            pos[i] = Vec2f(randomFloat(-halfW, halfW), randomFloat(-halfH, halfH));
            float speed = randomFloat(minSpeed, maxSpeed);
            float phi = randomFloat(0.0f, 2.0f * PI);
            vel[i] = Vec2f(speed * cosf(phi), speed * sinf(phi));
        }
    }

    void step(milliseconds_t dtMs)
    {
        physics::steppedSimulate(dtMs, MAX_SUBSTEP_MS, MAX_TOTAL_STEP_MS,
                                 [this](float dtSeconds) { substep(dtSeconds); });
    }

   private:
    void substep(float dtSeconds)
    {
        for (size_t i = 0; i < pos.size(); i++)
        {
            pos[i] += vel[i] * dtSeconds;
            reflect(pos[i].x, vel[i].x, halfW);
            reflect(pos[i].y, vel[i].y, halfH);
        }
    }

    // Elastic reflection off +/-limit: fold the position back inside and flip the
    // velocity component. The trailing clamp is a guard for the degenerate case
    // where one huge dt (or a zero-size box) would leave the point past the
    // opposite wall even after folding.
    static void reflect(float& p, float& v, float limit)
    {
        if (limit <= 0.0f)
        {
            p = 0.0f;
            return;
        }
        if (p > limit)
        {
            p = 2.0f * limit - p;
            v = -v;
        }
        else if (p < -limit)
        {
            p = -2.0f * limit - p;
            v = -v;
        }
        if (p > limit) p = limit;
        if (p < -limit) p = -limit;
    }
};
