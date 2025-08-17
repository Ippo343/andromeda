#pragma once

#include <ArduinoLog.h>
#include <FastLED.h>

#include "geometry.h"
#include "utils.h"
#include "moodlight.h"
#include "control-hints.h"
#include "effects.h"
#include "animations.h"

#ifdef ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Web command enum for the web server
enum class Command {
    NEXT = 'N',
    HOLD = 'H',
    POWER_OFF = 'D',
    WHITE = 'W'
};

#endif

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
    milliseconds_t FADE_IN_DURATION  = 2500;
    milliseconds_t FADE_OUT_DURATION = 5000;
    milliseconds_t MIN_EFFECT_DURATION = 2 MINUTES;
    milliseconds_t MAX_EFFECT_DURATION = 10 MINUTES;

    // These contain the actual timestamps to plan the fade in/out and the transition to the next effect
    milliseconds_t effectStart = 0;       // time when the current effect started
    milliseconds_t nextTransition = 0;    // time when the current effect will end
    milliseconds_t fadeInEnd;             // time when the fade in will end ( = effectStart + FADE_IN_DURATION)
    milliseconds_t fadeOutStart;          // time when the fade out will start ( = nextTransition - FADE_OUT_DURATION)

    void update(milliseconds_t t);

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

#ifdef ESP32
    // Initialize the web command queue
    void initWebQueue();

    // Process any pending web commands
    void processWebCommands();

    // Queue a web command from the web server
    bool queueWebCommand(Command command);
#endif

#ifdef ESP32
  // The following methods are called directly and synchronously on the R4,
  // but they must not be called directly on the ESP32 and must be queued instead.
  // It's pretty nifty that you can change the accessibility of these methods with a define.
  // Lowkey liking C++ rn.
  private:
#elif defined(ARDUINO_R4_WIFI)
  public:
#endif
    void holdEffect();

    void powerOff();

    void staticWhite();

    // When the transition time is reached:
    // - play an animation
    // - pick a new effect
    // - pick the next transition time
    // - and also sprinkle random rotation transforms here and there
    void handleTransition(AbstractEffect* nextEffect = nullptr, bool playAnimation = true);

  private:
    MissionControl() = default;

#ifdef ESP32
    QueueHandle_t webCommandQueue = nullptr;
    static constexpr int WEB_QUEUE_SIZE = 10;
#endif
};
