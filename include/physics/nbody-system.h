#pragma once

// Real mutual-gravity N-body simulation for the RGBodyProblem effect.
//
// Integrated with velocity Verlet (leapfrog, kick-drift-kick) rather than RK4: this
// runs indefinitely, and RK4 isn't symplectic, so it slowly leaks/gains energy over
// long runtimes (orbits drift). Velocity Verlet is symplectic (bounded long-term
// energy error) and cheaper (2 force evaluations per step vs RK4's 4).
//
// Three known N-body failure modes are addressed deliberately, sacrificing some
// physical purity for indefinite visual reliability:
//  - Plummer force softening prevents a singular force spike (and resulting ejection)
//    when two bodies pass very close to each other.
//  - Initial velocities are momentum-zeroed so the system doesn't systematically drift
//    off-screen as a whole.
//  - A very weak centering "leash" plus escape detection + full reseed handles the
//    case where the system (or one body) becomes permanently unbound anyway.

#include <math.h>

#include <vector>

#include "physics/physics-random.h"
#include "physics/substepper.h"
#include "physics/vec2f.h"
#include "utils.h"

class NBodySystem
{
   public:
    std::vector<Vec2f> pos, vel;
    std::vector<float> mass;
    float boundsRadius = 0;

    static constexpr float G = 1.5e5f;                  // tuning constant, needs a visual pass
    static constexpr float SOFTENING_MM = 30.0f;        // Plummer softening length
    static constexpr float LEASH_ACCEL_PER_MM = 0.05f;  // tuning constant, needs a visual pass
    static constexpr float ESCAPE_DISTANCE_MARGIN = 1.5f;

    void initRandom(size_t n, float boundsRadiusMm, float minMass, float maxMass, float minSpeed,
                    float maxSpeed)
    {
        boundsRadius = boundsRadiusMm;
        minMass_ = minMass;
        maxMass_ = maxMass;
        minSpeed_ = minSpeed;
        maxSpeed_ = maxSpeed;

        pos.assign(n, Vec2f());
        vel.assign(n, Vec2f());
        mass.assign(n, 0.0f);
        accel_.assign(n, Vec2f());
        hasAccel_ = false;

        for (size_t i = 0; i < n; i++)
        {
            mass[i] = randomFloat(minMass, maxMass);

            float r = randomFloat(0.1f, 0.5f) * boundsRadius;
            float theta = randomFloat(0.0f, 2.0f * PI);
            pos[i] = Vec2f(r * cosf(theta), r * sinf(theta));

            float speed = randomFloat(minSpeed, maxSpeed);
            float phi = randomFloat(0.0f, 2.0f * PI);
            vel[i] = Vec2f(speed * cosf(phi), speed * sinf(phi));
        }

        zeroNetMomentum();
    }

    // Returns true if a reseed happened this call (the system - or one body - became
    // permanently unbound and was regenerated from scratch).
    bool step(milliseconds_t dtMs)
    {
        physics::steppedSimulate(dtMs, physics::DEFAULT_MAX_SUBSTEP_MS,
                                 physics::DEFAULT_MAX_TOTAL_STEP_MS,
                                 [this](float dtSeconds) { substep(dtSeconds); });

        if (detectEscape())
        {
            // Same procedure as construction, same N/ranges. Colors are owned by the
            // effect, not this class, so they're untouched by a reseed - cosmetic
            // identity persists across a dynamical reset.
            initRandom(pos.size(), boundsRadius, minMass_, maxMass_, minSpeed_, maxSpeed_);
            return true;
        }
        return false;
    }

   private:
    float minMass_ = 0, maxMass_ = 0, minSpeed_ = 0, maxSpeed_ = 0;

    // Scratch buffer for accelerationOn(), reused across substeps so substep() never
    // allocates in the per-frame hot path. Also lets substep() carry the final
    // acceleration of one call forward as the next call's initial acceleration
    // (positions don't move between calls, so it's mathematically identical to
    // recomputing it) instead of doing two full O(n^2) force sweeps every call.
    std::vector<Vec2f> accel_;
    bool hasAccel_ = false;

    Vec2f accelerationOn(size_t i) const
    {
        Vec2f a(0, 0);
        for (size_t j = 0; j < pos.size(); j++)
        {
            if (j == i) continue;
            Vec2f rij = pos[j] - pos[i];
            float distSq = rij.lengthSquared() + SOFTENING_MM * SOFTENING_MM;
            float invDist3 = 1.0f / (distSq * sqrtf(distSq));
            a += rij * (G * mass[j] * invDist3);
        }
        a += pos[i] * (-LEASH_ACCEL_PER_MM);
        return a;
    }

    // Velocity Verlet / leapfrog (kick-drift-kick). accel_ holds the "initial"
    // acceleration for this substep: on the very first substep since init/reseed it
    // must be computed fresh, but on every subsequent substep it's already sitting in
    // accel_ as the previous substep's final (post-drift) acceleration - positions
    // don't change between calls, so recomputing it would just repeat the same O(n^2)
    // sweep for an identical result.
    void substep(float dtSeconds)
    {
        size_t n = pos.size();
        // Tests (and any other caller) may assign pos/vel/mass directly rather than
        // going through initRandom() - keep accel_ in sync rather than assuming it
        // always tracks pos's size.
        if (accel_.size() != n)
        {
            accel_.assign(n, Vec2f());
            hasAccel_ = false;
        }
        if (!hasAccel_)
        {
            for (size_t i = 0; i < n; i++) accel_[i] = accelerationOn(i);
            hasAccel_ = true;
        }

        for (size_t i = 0; i < n; i++)
        {
            vel[i] += accel_[i] * (dtSeconds * 0.5f);  // kick
            pos[i] += vel[i] * dtSeconds;              // drift
        }

        for (size_t i = 0; i < n; i++) accel_[i] = accelerationOn(i);  // recompute at new positions
        for (size_t i = 0; i < n; i++) vel[i] += accel_[i] * (dtSeconds * 0.5f);  // kick
    }

    void zeroNetMomentum()
    {
        Vec2f totalP(0, 0);
        float totalMass = 0;
        for (size_t i = 0; i < pos.size(); i++)
        {
            totalP += vel[i] * mass[i];
            totalMass += mass[i];
        }
        if (totalMass < 1e-9f) return;

        Vec2f vCM = totalP * (1.0f / totalMass);
        for (size_t i = 0; i < vel.size(); i++) vel[i] -= vCM;
    }

    // Heuristic instability check, not physically exact per-pair energy: treats the
    // rest of the system (excluding the body under test) as concentrated at its own
    // center of mass, which is sufficient for its only purpose (deciding when to
    // self-heal), not for accurate dynamics.
    bool detectEscape() const
    {
        size_t n = pos.size();
        if (n < 2) return false;

        float totalMass = 0;
        for (size_t i = 0; i < n; i++) totalMass += mass[i];
        if (totalMass < 1e-9f) return false;

        for (size_t i = 0; i < n; i++)
        {
            if (pos[i].length() < boundsRadius * ESCAPE_DISTANCE_MARGIN) continue;

            float remainingMass = totalMass - mass[i];
            if (remainingMass < 1e-9f) continue;

            // COM of the rest of the system, excluding body i itself - including it
            // would pull the COM toward i and understate its true distance from the
            // rest of the system, inflating |pe| and masking a real escape.
            Vec2f comRest(0, 0);
            for (size_t j = 0; j < n; j++)
            {
                if (j == i) continue;
                comRest += pos[j] * mass[j];
            }
            comRest = comRest * (1.0f / remainingMass);

            float distToCom = (pos[i] - comRest).length();
            float pe = -G * mass[i] * remainingMass / (distToCom + SOFTENING_MM);
            float ke = 0.5f * mass[i] * vel[i].lengthSquared();
            float specificEnergy = (ke + pe) / mass[i];
            if (specificEnergy > 0.0f) return true;
        }
        return false;
    }
};
