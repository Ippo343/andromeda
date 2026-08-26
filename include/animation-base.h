#pragma once

#include "control-hints.h"
#include "effects-utils.h"
#include "geometry/geometry.h"
#include "perf-monitor.h"
#include "utils.h"

// Abstract base class for animations that take full, synchronous control of
// the strips: run() is expected to block (own loop, delay(), FASTLED_SHOW())
// until the animation is finished. Only used for the boot/status indicators
// (WiFiConnectingAnimation, WiFiSuccessAnimation, ErrorAnimation) driven
// directly from main.cpp before the web server/render loop is even up, where
// blocking is harmless. The rotation animations MissionControl plays between
// effects use AbstractFrameAnimation (animation-frame-base.h) instead, which
// renders one frame at a time so the render loop stays responsive - see that
// header for why the two are kept separate rather than unified.
class AbstractBlockingAnimation
{
   public:
    virtual const char* GetName();

    control_hints_t controlHints = ControlHints::NONE;

    virtual ~AbstractBlockingAnimation() {}

    virtual void run();

    // Reset all buffers to black and brightness to max,
    // to prevent funny inputs going into the next effect
    virtual void cleanup()
    {
        paint(CRGB::Black);
        FASTLED_SHOW();
    }
};