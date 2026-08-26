#pragma once

// Generalizes the frame-rate-independent dt-tracking idiom established in
// ElectricSparks (include/effects/electric-sparks.h): effects that need a real
// per-frame dt (not FastLED's wall-clock-coupled beatsin8) must self-track the
// previous absolute timestamp, since `t` is always an absolute millis() value, never
// a delta.

#include "utils.h"

struct FrameClock
{
    milliseconds_t lastT = 0;
    bool hasLastT = false;

    // Returns dt (ms) since the previous tick(); 16ms fallback on the first call.
    // Clamped to maxDt as cheap hitch protection at the effect boundary, in addition
    // to whatever internal substep clamping the physics module itself applies.
    milliseconds_t tick(milliseconds_t t, milliseconds_t maxDt = 250)
    {
        milliseconds_t dt = hasLastT ? (t - lastT) : 16;
        lastT = t;
        hasLastT = true;
        return dt > maxDt ? maxDt : dt;
    }
};
