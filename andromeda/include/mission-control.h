#pragma once

#include <ArduinoLog.h>
#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "animations.h"
#include "control-hints.h"
#include "effects.h"
#include "geometry.h"
#include "moodlight.h"
#include "utils.h"
#include "perf-monitor.h"

// Web command enum for the web server
enum class Command {
    NEXT = 'N',
    HOLD = 'H',
    POWER_OFF = 'D',
    WHITE = 'W'
};

class MissionControl
{
  public:

    static inline MissionControl& Instance()
    {
      static MissionControl instance;
      return instance;
    }

    // Prevent copy/move construction
    MissionControl(const MissionControl&) = delete;
    MissionControl& operator=(const MissionControl&) = delete;

    // The main update loop: renders a new frame and then checks for new commands
    void update(milliseconds_t t);

    // Queue a web command from the comms
    bool queueWebCommand(Command command);

  private:

    // The effect that is currently running
    AbstractEffect* effect;

    // Main ON/OFF switch. If OFF, power down and do nothing.
    bool ON = true;

    // These parameters control how long an effect lasts and how quickly it fades in and out
    milliseconds_t FADE_IN_DURATION  = 2500;
    milliseconds_t FADE_OUT_DURATION = 5000;
    milliseconds_t MIN_EFFECT_DURATION = 2 MINUTES;
    milliseconds_t MAX_EFFECT_DURATION = 10 MINUTES;

    // These contain the actual timestamps to plan the fade in/out and the transition to the next effect
    milliseconds_t effectStart = 0;       // time when the current effect started
    milliseconds_t nextTransition = 0;    // time when the current effect will end
    milliseconds_t fadeInEnd;             // time when the fade in will end ( = effectStart + FADE_IN_DURATION)
    milliseconds_t fadeOutStart;          // time when the fade out will start ( = nextTransition - FADE_OUT_DURATION)

    // Picks a new transition time and resets the other timestamps accordingly
    void setNextTransition();

    // Return the master brightness to fade the effects in and out
    //
    //   fadeInEnd   fadeOutStart
    //     /¯¯¯¯¯¯¯¯¯¯¯¯¯¯\
    //    /                \
    // effectStart      nextTransition
    //
    byte getBrightness(milliseconds_t t);

    // Pick a new random animation, play it, and deallocate it
    void runRandomAnimation();

    // Hold the current effect forever
    void holdEffect();

    // Power off the LEDs and wait
    void powerOff();

    // Switch to a static white color and hold forever (lamp mode)
    void staticWhite();

    // When the transition time is reached:
    // - play an animation
    // - pick a new effect
    // - pick the next transition time
    // - and also sprinkle random rotation transforms here and there
    void handleTransition(AbstractEffect* nextEffect = nullptr, bool playAnimation = true);

    // Process any pending web commands
    void processWebCommands();

    // Initialize the web command queue
    void initWebQueue();

    MissionControl() { initWebQueue(); }

    QueueHandle_t webCommandQueue = nullptr;
    static constexpr int WEB_QUEUE_SIZE = 10;
};
