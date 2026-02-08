#pragma once

#include "control-hints.h"
#include "effects-utils.h"
#include "geometry/geometry.h"
#include "perf-monitor.h"
#include "utils.h"

// Abstract base class for all animations.
// Animations are not really effects, they are meant to be small programs
// that take direct control of all the strips and run once.
// The control flow will not loop them, it will use them as transitions
// between different effects.
class AbstractAnimation
{
  public:

    virtual const char* GetName();

    control_hints_t controlHints = ControlHints::NONE;

    virtual ~AbstractAnimation() { }

    virtual void run();

    // Reset all buffers to black and brightness to max,
    // to prevent funny inputs going into the next effect
    virtual void cleanup()
    {
      paint(CRGB::Black);
      FASTLED_SHOW();
    }
};