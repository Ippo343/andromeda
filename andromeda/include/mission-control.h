#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

#include <ArduinoLog.h>
#include <FastLED.h>

#include "geometry.h"
#include "utils.h"
#include "moodlight.h"
#include "control-hints.h"
#include "effects.h"
#include "animations.h"

class MissionControl
{
  public:

    static MissionControl& Instance()
    {
      static MissionControl instance;
      return instance;
    }

    // Prevent copy/move construction
    MissionControl(const MissionControl&) = delete;
    MissionControl& operator=(const MissionControl&) = delete;

    // The effect that is currently running
    AbstractEffect*    effect;

    // Main ON/OFF switch. If OFF, power down and do nothing.
    bool ON = true;

    // These parameters control how long an effect lasts and how quickly it fades in and out
    milliseconds FADE_IN_DURATION  = 2500;
    milliseconds FADE_OUT_DURATION = 5000;
    milliseconds MIN_EFFECT_DURATION = 2 MINUTES;
    milliseconds MAX_EFFECT_DURATION = 10 MINUTES;

    // These contain the actual timestamps to plan the fade in/out and the transition to the next effect
    milliseconds effectStart = 0;       // time when the current effect started
    milliseconds nextTransition = 0;    // time when the current effect will end
    milliseconds fadeInEnd;             // time when the fade in will end ( = effectStart + FADE_IN_DURATION)
    milliseconds fadeOutStart;          // time when the fade out will start ( = nextTransition - FADE_OUT_DURATION)

    // Picks a new transition time and resets the other timestamps accordingly
    void setNextTransition();

    // Return the master brightness to fade the effects in and out
    //
    //   fadeInEnd   fadeOutStart
    //     /¯¯¯¯¯¯¯¯¯¯¯¯¯¯\
    //    /                \
    // effectStart      nextTransition
    //
    byte getBrightness(milliseconds t);

    // Pick a new random animation, play it, and deallocate it
    void runRandomAnimation();

    // When the transition time is reached:
    // - play an animation
    // - pick a new effect
    // - pick the next transition time
    // - and also sprinkle random rotation transforms here and there
    void handleTransition();

    void holdEffect();

    void powerOff();

    void powerOn();

    void update(milliseconds t);

    private:
      MissionControl() = default;
};

#endif