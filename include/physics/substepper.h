#pragma once

// Fixed-cadence substep loop shared by every physics module that needs frame-hitch
// protection: caps the total dt consumed per call, then substeps at a fixed cadence,
// so a stall never causes an integrator's dt/dt^2 terms to blow up or forces a
// "catch-up" backlog. NBodySystem and VerletChain both need exactly this and nothing
// module-specific, so it's factored out here instead of being copy-pasted per class.

#include "utils.h"

namespace physics
{
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
