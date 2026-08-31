#pragma once

// Fixed-cadence substep loop shared by every physics module that needs frame-hitch
// protection: caps the total dt consumed per call, then substeps at a fixed cadence,
// so a stall never causes an integrator's dt/dt^2 terms to blow up or forces a
// "catch-up" backlog. NBodySystem and VerletChain both need exactly this and nothing
// module-specific, so it's factored out here instead of being copy-pasted per class.

#include "utils.h"

namespace physics
{
// Shared default caps for the four modules that call steppedSimulate() with no
// module-specific reason to differ: 16ms substeps (well inside every integrator's
// stability margin), capped at 64ms of total catch-up per call (a few dropped frames'
// worth) so a long stall substeps forward at a bounded cost instead of free-running.
constexpr milliseconds_t DEFAULT_MAX_SUBSTEP_MS = 16;
constexpr milliseconds_t DEFAULT_MAX_TOTAL_STEP_MS = 64;

template <class F>
void steppedSimulate(milliseconds_t dtMs, milliseconds_t maxSubstepMs, milliseconds_t maxTotalMs,
                     F&& substepFn)
{
    milliseconds_t remaining = dtMs > maxTotalMs ? maxTotalMs : dtMs;
    while (remaining > 0)
    {
        milliseconds_t sub = remaining > maxSubstepMs ? maxSubstepMs : remaining;
        substepFn(sub / 1000.0f);
        remaining -= sub;
    }
}
}  // namespace physics
