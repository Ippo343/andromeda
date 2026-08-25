#pragma once

// N-link rigid pendulum chain, modeled as Cartesian point masses connected by
// fixed-length rod constraints (the classic "Jakobsen / Advanced Character Physics"
// rope-chain technique), rather than angle-based Lagrangian mechanics with a mass
// matrix. This sidesteps deriving new equations of motion per N entirely, needs no
// matrix inversion, and - critically for a frictionless simulation that runs
// indefinitely - stays energy-stable over long runtimes the way a naive RK4-on-angles
// integrator would not.
//
// Mass 0's rod connects to a fixed `anchor` (the render-area center - deliberately not
// a physical "up" direction, which is arbitrary for this hardware); rod i connects
// mass i-1 to mass i.

#include <math.h>

#include <vector>

#include "physics/physics-random.h"
#include "physics/vec2f.h"
#include "utils.h"

class VerletChain
{
   public:
    Vec2f anchor;
    std::vector<Vec2f> curr, prev;  // current & previous position per mass (implicit velocity)
    std::vector<float> invMass;
    std::vector<float> rodLength;

    static constexpr float GRAVITY_MM_PER_S2 = 4000.0f;  // tuning constant, needs a visual pass
    static constexpr int CONSTRAINT_ITERATIONS = 6;
    static constexpr milliseconds_t MAX_SUBSTEP_MS = 16;
    static constexpr milliseconds_t MAX_TOTAL_STEP_MS = 64;

    // Frictionless by design: no damping is applied anywhere in this class.
    void initRandom(size_t n, Vec2f anchorPoint, float minRodLen, float maxRodLen, float minMass,
                    float maxMass)
    {
        anchor = anchorPoint;
        curr.assign(n, Vec2f());
        prev.assign(n, Vec2f());
        invMass.assign(n, 0.0f);
        rodLength.assign(n, 0.0f);
        previousSubstepDtSeconds = 1.0f / 60.0f;

        Vec2f prevPoint = anchor;
        for (size_t i = 0; i < n; i++)
        {
            float mass = randomFloat(minMass, maxMass);
            invMass[i] = 1.0f / mass;
            rodLength[i] = randomFloat(minRodLen, maxRodLen);

            // Any starting angle is valid for a chaotic pendulum - unconstrained.
            float angle = randomFloat(-PI, PI);
            Vec2f dir(sinf(angle), cosf(angle));
            Vec2f pos = prevPoint + dir * rodLength[i];

            // Zero initial velocity (curr == prev): no first-frame jump.
            curr[i] = pos;
            prev[i] = pos;
            prevPoint = pos;
        }
    }

    // Substeps at a fixed ~16ms cadence, hard-capped total per call, so a frame hitch
    // never causes the a*dt^2 term to blow up or forces a "catch-up" backlog.
    void step(milliseconds_t dtMs)
    {
        milliseconds_t remaining = dtMs > MAX_TOTAL_STEP_MS ? MAX_TOTAL_STEP_MS : dtMs;
        while (remaining > 0)
        {
            milliseconds_t sub = remaining > MAX_SUBSTEP_MS ? MAX_SUBSTEP_MS : remaining;
            verletIntegrate(sub / 1000.0f);
            for (int i = 0; i < CONSTRAINT_ITERATIONS; i++) relaxConstraints();
            remaining -= sub;
        }
    }

   private:
    float previousSubstepDtSeconds = 1.0f / 60.0f;

    // Time-corrected Verlet update. The textbook fixed-dt form
    // (x_next = 2*x_curr - x_prev + a*dt^2) is wrong under irregular dt - this
    // variable-timestep form scales the implicit velocity term by the ratio of the
    // current to the previous substep's duration instead of assuming they're equal.
    void verletIntegrate(float dtSeconds)
    {
        float ratio =
            previousSubstepDtSeconds > 1e-6f ? dtSeconds / previousSubstepDtSeconds : 1.0f;
        Vec2f accel(0, GRAVITY_MM_PER_S2);
        float dtSquared = dtSeconds * dtSeconds;

        for (size_t i = 0; i < curr.size(); i++)
        {
            Vec2f velocityTerm = (curr[i] - prev[i]) * ratio;
            Vec2f next = curr[i] + velocityTerm + accel * dtSquared;
            prev[i] = curr[i];
            curr[i] = next;
        }

        previousSubstepDtSeconds = dtSeconds;
    }

    // Gauss-Seidel, in-place so later rods see earlier corrections within the same
    // iteration (aids convergence) - standard for rope-chain relaxation.
    void relaxConstraints()
    {
        satisfyConstraint(anchor, 0.0f, curr[0], invMass[0], rodLength[0]);
        for (size_t i = 1; i < curr.size(); i++)
            satisfyConstraint(curr[i - 1], invMass[i - 1], curr[i], invMass[i], rodLength[i]);
    }

    // Inverse-mass-weighted correction: a heavier mass moves less for a given rod-
    // length violation. invMassA == 0 (the anchor) means pa never moves.
    static void satisfyConstraint(Vec2f& pa, float invMassA, Vec2f& pb, float invMassB,
                                  float restLength)
    {
        Vec2f delta = pb - pa;
        float dist = delta.length();
        if (dist < 1e-6f) return;  // coincident points: no well-defined correction direction

        float totalInvMass = invMassA + invMassB;
        if (totalInvMass < 1e-9f) return;  // both points immovable

        float diff = (dist - restLength) / dist;
        Vec2f correction = delta * diff;
        pa += correction * (invMassA / totalInvMass);
        pb -= correction * (invMassB / totalInvMass);
    }
};
