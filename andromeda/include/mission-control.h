#pragma once

#include <ArduinoLog.h>
#include <FastLED.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "animations.h"
#include "control-hints.h"
#include "effects.h"
#include "geometry/geometry.h"
#include "moodlight.h"
#include "perf-monitor.h"
#include "utils.h"

// Web command enum for the web server
enum class Command
{
    NEXT = 'N',
    HOLD = 'H',
    POWER_OFF = 'D',
    COLOR = 'C'
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

    // TODO: this is different from "public uint8_t maxBrightness"... how?
    inline uint8_t getMaxBrightness() const { return maxBrightness; }
    inline void setMaxBrightness(uint8_t b) { maxBrightness = b; }

    CRGB staticColor = CRGB::White;

    // 80 MHz seems to be the minimum frequency for the WiFi and LED drivers to work,
    // at least on the C3 where I tried messing with it. At 40MHz nothing works lol.
    const uint32_t MIN_CPU_FREQ_MHZ = 80;
    // The max is instead a FastLED macro that adjusts for each chipset.
    uint32_t MAX_CPU_FREQ_MHZ = F_CPU_MHZ;

    inline void setMinCpuFrequency()
    {
        Log.noticeln("Lowering CPU frequency to %d MHz", MIN_CPU_FREQ_MHZ);
        setCpuFrequencyMhz(MIN_CPU_FREQ_MHZ);
    }

    // With an argument, it both sets the frequency and updates the freq cap
    inline void setMaxCpuFrequency(uint32_t freq)
    {
        Log.noticeln("Capping max CPU frequency to %d MHz", freq);
        MAX_CPU_FREQ_MHZ = freq;
        setCpuFrequencyMhz(MAX_CPU_FREQ_MHZ);
    }

    // Without an argument, it just sets the frequency to the current cap
    inline void setMaxCpuFrequency()
    {
        Log.noticeln("Raising CPU frequency to %d MHz", MAX_CPU_FREQ_MHZ);
        setCpuFrequencyMhz(MAX_CPU_FREQ_MHZ);
    }

   private:
    // The effect that is currently running
    AbstractEffect* effect;

    // Main ON/OFF switch. If OFF, power down and do nothing.
    bool ON = true;

    // Maximum allowed brightness (0-255)
    // Note that this is different from FastLED's global brightness,
    // which is also used for the fade in/out ramps.
    uint8_t maxBrightness = 255;

    // These parameters control how long an effect lasts and how quickly it fades in and out
    milliseconds_t FADE_IN_DURATION = 2500;
    milliseconds_t FADE_OUT_DURATION = 5000;
    milliseconds_t MIN_EFFECT_DURATION = 2 MINUTES;
    milliseconds_t MAX_EFFECT_DURATION = 10 MINUTES;

    // These contain the actual timestamps to plan the fade in/out and the transition to the next
    // effect
    milliseconds_t effectStart = 0;     // time when the current effect started
    milliseconds_t nextTransition = 0;  // time when the current effect will end
    milliseconds_t fadeInEnd;  // time when the fade in will end ( = effectStart + FADE_IN_DURATION)
    milliseconds_t
        fadeOutStart;  // time when the fade out will start ( = nextTransition - FADE_OUT_DURATION)

    // Picks a new transition time and resets the other timestamps accordingly
    void setNextTransition();

    // Return the master brightness to fade the effects in and out
    //
    //   fadeInEnd   fadeOutStart
    //     /¯¯¯¯¯¯¯¯¯¯¯¯¯¯\
    //    /                \
    // effectStart      nextTransition
    //
    uint8_t calcBrightness(milliseconds_t t);

    // Pick a new random animation, play it, and deallocate it
    void runRandomAnimation();

    // Hold the current effect forever
    void holdEffect();

    // Power off the LEDs and wait
    void powerOff();

    // Switch to a static color and hold forever (lamp mode)
    void transitionToStaticColor();

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
