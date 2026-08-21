#pragma once

#include "geometry/geometry.h"  // FULL_CIRCLE

// Pure per-LED ramp math extracted from BaseSweep::colorSweep (animations.cpp),
// kept free of GEOMETRY/FastLED/timing so it's natively unit-testable in
// isolation - mirrors comms-utils.h/ws-command-parser.h's extraction pattern.
// animations.cpp itself stays excluded from native builds (its run() methods
// drive real delay()/millis() loops), but this one piece of branchy boundary
// math has no such dependency: given the sweep's current leading/trailing
// edge and a single LED's coordinate, it decides whether that LED is inside
// the sweep's ramp and how far from the leading edge it is - the same
// question for both the angular (wraparound, maxCoord == FULL_CIRCLE) and
// radial (no wraparound) cases BaseSweep's two subclasses (ClockSweep,
// RadialSweep) share.
//
// coordLead/coordTail are `long` rather than `short`: writing this test
// table surfaced that the original inline code stored them in `short`
// (max 32767), but map()'s output range for an angular sweep is
// [0, FULL_CIRCLE + rampWidth] = up to ~37000 - silently overflowing near
// the end of every ClockSweep. Fixed here since colorSweep() (the only
// caller) is being touched anyway for this extraction.

struct SweepRampResult
{
    bool inRange;
    long rampDistance;
};

inline SweepRampResult computeSweepRamp(long coordLead, long coordTail, unsigned short coord,
                                        unsigned short maxCoord, unsigned short rampWidth)
{
    bool angular = (maxCoord == FULL_CIRCLE);

    bool inRange;
    if (coordLead > maxCoord)
    {
        // Handle overflow at the boundary
        inRange = angular ? ((coord >= coordTail) || (coord <= (coordLead - maxCoord)))
                          : (coord >= coordTail);
    }
    else if (coordTail < 0)
    {
        // Handle negative tail at the beginning of sweep
        inRange = angular ? ((coord >= (coordTail + maxCoord)) || (coord <= coordLead))
                          : (coord <= coordLead);
    }
    else
    {
        // Normal case - no boundary issues
        inRange = (coord >= coordTail && coord <= coordLead);
    }

    if (!inRange) return {false, 0};

    long rampDistance;
    if (angular && coordLead > maxCoord && coord <= (coordLead - maxCoord))
    {
        // LED is in the wrapped portion (angular case)
        rampDistance = (coordLead - maxCoord) - coord;
    }
    else if (angular && coordTail < 0 && coord >= (coordTail + maxCoord))
    {
        // LED is in the wrapped portion (negative tail case, angular)
        rampDistance = coordLead - (coord - maxCoord);
    }
    else
    {
        // Normal case (both angular and radial)
        rampDistance = coordLead - coord;
    }

    return {true, rampDistance};
}
