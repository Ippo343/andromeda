#pragma once

#include "control-hints.h"
#include "utils.h"

// Abstract base class for the animations MissionControl plays as transitions
// between effects (see mission-control.cpp's handleTransition()). Unlike
// AbstractBlockingAnimation (animation-base.h), these animations never call
// delay()/FASTLED_SHOW() and never own a loop: MissionControl::update() calls
// renderFrame() once per tick, exactly like it drives AbstractEffect, so the
// render loop keeps processing web commands (NEXT/HOLD/COLOR/POWER_OFF/...)
// while a transition animation plays instead of blocking on it.
class AbstractFrameAnimation
{
   public:
    virtual const char* GetName() = 0;

    control_hints_t controlHints = ControlHints::NONE;

    virtual ~AbstractFrameAnimation() {}

    // Renders exactly one frame into the strip buffers for localT ms elapsed
    // since this animation instance started (t=0 at construction). Returns
    // true once the animation has finished; MissionControl stops calling
    // renderFrame() and tears the instance down on the first true.
    virtual bool renderFrame(milliseconds_t localT) = 0;
};
