#pragma once

// Point masses bouncing elastically inside an axis-aligned box, at constant speed
// so the simulation is energy-stable for indefinite runtime (no damping, no
// forces - just free flight between wall reflections). The reflection is
// specular by default, but an optional per-bounce angular jitter can be dialled
// in (setBounceJitter) so a ball never locks into a short periodic billiard
// orbit - the kick rotates the heading only, speed is untouched. Written for the
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

    // Max random heading change applied on each wall bounce, in radians. 0 (the
    // default) is exact specular reflection. A small value keeps the motion
    // DVD-logo-like while stopping a ball settling into a fixed rectangular orbit.
    float bounceJitter = 0.0f;
    void setBounceJitter(float radians) { bounceJitter = radians; }

    // Wall-contact record, one per ball. `hitPoint` is where the last bounce
    // touched the box edge (the reflecting component snapped to the wall);
    // `sinceHit` is seconds elapsed since then, so a consumer can hold a brief
    // "in contact" flash. sinceHit starts large so nothing flashes at startup.
    std::vector<Vec2f> hitPoint;
    std::vector<float> sinceHit;

    static constexpr milliseconds_t MAX_SUBSTEP_MS = 16;
    static constexpr milliseconds_t MAX_TOTAL_STEP_MS = 64;

    void initRandom(size_t n, float halfWidthMm, float halfHeightMm, float minSpeed, float maxSpeed)
    {
        halfW = halfWidthMm;
        halfH = halfHeightMm;
        pos.assign(n, Vec2f());
        vel.assign(n, Vec2f());
        hitPoint.assign(n, Vec2f());
        sinceHit.assign(n, 1e9f);

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
            bool hx = reflect(pos[i].x, vel[i].x, halfW);
            bool hy = reflect(pos[i].y, vel[i].y, halfH);
            if (hx || hy)
            {
                Vec2f p = pos[i];
                if (hx) p.x = vel[i].x > 0.0f ? -halfW : halfW;  // vel already flipped
                if (hy) p.y = vel[i].y > 0.0f ? -halfH : halfH;
                hitPoint[i] = p;
                sinceHit[i] = 0.0f;
                if (bounceJitter > 0.0f) perturb(vel[i]);
            }
            else { sinceHit[i] += dtSeconds; }
        }
    }

    // Rotate a velocity by a random angle in [-bounceJitter, +bounceJitter],
    // preserving its magnitude, then nudge the heading away from a pure
    // horizontal/vertical run so a ball can't end up sliding along a wall
    // forever (which would read as static). Called once per wall contact.
    void perturb(Vec2f& v) const
    {
        float speed = sqrtf(v.x * v.x + v.y * v.y);
        if (speed <= 0.0f) return;

        float ang = atan2f(v.y, v.x) + randomFloat(-bounceJitter, bounceJitter);
        v.x = speed * cosf(ang);
        v.y = speed * sinf(ang);

        const float minComponent = speed * 0.10f;  // ~5.7 deg clearance off each axis
        if (fabsf(v.x) < minComponent) v.x = (v.x >= 0.0f ? minComponent : -minComponent);
        if (fabsf(v.y) < minComponent) v.y = (v.y >= 0.0f ? minComponent : -minComponent);
        float s = sqrtf(v.x * v.x + v.y * v.y);
        v.x *= speed / s;
        v.y *= speed / s;
    }

    // Elastic reflection off +/-limit: fold the position back inside and flip the
    // velocity component. Returns true if it reflected this call. The trailing
    // clamp is a guard for the degenerate case where one huge dt (or a zero-size
    // box) would leave the point past the opposite wall even after folding.
    static bool reflect(float& p, float& v, float limit)
    {
        if (limit <= 0.0f)
        {
            p = 0.0f;
            return false;
        }
        bool hit = false;
        if (p > limit)
        {
            p = 2.0f * limit - p;
            v = -v;
            hit = true;
        }
        else if (p < -limit)
        {
            p = -2.0f * limit - p;
            v = -v;
            hit = true;
        }
        if (p > limit) p = limit;
        if (p < -limit) p = -limit;
        return hit;
    }
};
