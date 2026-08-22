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

// Persists the user-configured max brightness across reboots (NVS), mirroring
// FactoryConfig's model-ID storage (geometry.cpp). Kept separate from the
// per-frame MissionControl::setMaxBrightness()/getMaxBrightness() so the
// drag-speed WS updates (see comms.cpp) never touch flash - only the single
// "commit" message sent when a slider drag ends calls persist().
namespace BrightnessConfig
{
// Write the max brightness to persistent storage.
void persist(uint8_t value);

// Read the previously persisted max brightness, or `fallback` if never set.
uint8_t load(uint8_t fallback);
}  // namespace BrightnessConfig

// Web command types, dispatched by MissionControl::processWebCommands().
// BRIGHTNESS has no entry here: it never touches this queue (see comms.cpp's
// WS handler) since it's applied directly via setMaxBrightness() - the
// render loop re-reads maxBrightness every frame, so there's no correctness
// reason to funnel a drag's worth of updates through the queue and its
// per-command log line.
enum class CommandType : uint8_t
{
    NEXT,
    HOLD,
    RESUME,
    POWER_OFF,
    POWER_ON,
    COLOR,
    MODEL,
    REBOOT
};

inline const char* commandTypeToString(CommandType type)
{
    switch (type)
    {
        case CommandType::NEXT:
            return "NEXT";
        case CommandType::HOLD:
            return "HOLD";
        case CommandType::RESUME:
            return "RESUME";
        case CommandType::POWER_OFF:
            return "POWER_OFF";
        case CommandType::POWER_ON:
            return "POWER_ON";
        case CommandType::COLOR:
            return "COLOR";
        case CommandType::MODEL:
            return "MODEL";
        case CommandType::REBOOT:
            return "REBOOT";
    }
    return "UNKNOWN";
}

// Payload-carrying command sent from a web client into the render task's
// command queue. Flat POD (no union) so FreeRTOS's byte-copy queue
// semantics and CRGB's non-trivial ctor don't fight each other; the unused
// fields per command are a few wasted bytes at WEB_QUEUE_SIZE=10.
struct Command
{
    CommandType type;
    uint8_t r = 0, g = 0, b = 0;  // COLOR
    uint16_t modelId = 0;         // MODEL

    static Command Next() { return {CommandType::NEXT}; }
    static Command Hold() { return {CommandType::HOLD}; }
    static Command Resume() { return {CommandType::RESUME}; }
    static Command PowerOff() { return {CommandType::POWER_OFF}; }
    static Command PowerOn() { return {CommandType::POWER_ON}; }
    static Command Reboot() { return {CommandType::REBOOT}; }
    static Command Color(uint8_t r, uint8_t g, uint8_t b)
    {
        Command c{CommandType::COLOR};
        c.r = r;
        c.g = g;
        c.b = b;
        return c;
    }
    static Command Model(uint16_t id)
    {
        Command c{CommandType::MODEL};
        c.modelId = id;
        return c;
    }
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
    inline void setMaxBrightness(uint8_t b)
    {
        maxBrightness = b;
        stateDirty = true;
    }

    inline bool isOn() const { return ON; }
    inline bool isHolding() const { return holding; }
    inline const char* getEffectName() const { return effect ? effect->GetName() : "none"; }

    // Consumes (reads and clears) the flag set whenever broadcast-worthy state
    // changes, so Comms can poll it from its own task to know when to push a
    // fresh state message over the WebSocket.
    inline bool consumeStateDirty()
    {
        bool d = stateDirty;
        stateDirty = false;
        return d;
    }

    CRGB staticColor = CRGB::White;

    inline bool isColorActive() const { return effect && effect->wantsLiveColorUpdates(); }

    // Fast path for isColorActive() callers: update() re-reads staticColor
    // every frame and pushes it into the live effect, so a color drag can
    // write it directly from the network task instead of round-tripping
    // through the command queue - restores the pre-queue slider/wheel
    // responsiveness without reintroducing a race (only safe once an effect
    // is already live; the initial mode switch still needs to queue through
    // transitionToStaticColor()).
    inline void setLiveColor(uint8_t r, uint8_t g, uint8_t b)
    {
        staticColor = CRGB(r, g, b);
        stateDirty = true;
    }

    // Set once at boot from the model's preferred CPU frequency (main.cpp) and
    // never touched again - the main loop no longer scales the CPU frequency
    // up/down with ON/OFF or transitions.
    inline void setCpuFrequency(uint32_t freq)
    {
        Log.noticeln("Setting CPU frequency to %d MHz", freq);
        setCpuFrequencyMhz(freq);
    }

    inline void setFrameDurationCap(milliseconds_t duration)
    {
        Log.noticeln("Capping min frame duration to %d ms", duration);
        MIN_FRAME_DURATION_MS = duration;
    }

   private:
    // The effect that is currently running
    AbstractEffect* effect = nullptr;

    // Main ON/OFF switch. If OFF, power down and do nothing.
    bool ON = true;

    // True while the current effect is held indefinitely (nextTransition
    // pinned to the max value by holdEffect()). Mirrors ON for the web UI's
    // HOLD/RESUME button - see resumeEffect() for how un-holding picks a new
    // transition time.
    bool holding = false;

    // Maximum allowed brightness (0-255)
    // Note that this is different from FastLED's global brightness,
    // which is also used for the fade in/out ramps.
    uint8_t maxBrightness = 255;

    // Set whenever broadcast-worthy state changes; cleared by consumeStateDirty().
    // volatile because it's written from the render task and read/cleared from
    // Comms' web server task - mirrors the existing Comms::scanInProgress/
    // scanComplete cross-task flag pattern.
    volatile bool stateDirty = false;

    // These parameters control how long an effect lasts and how quickly it fades in and out
    milliseconds_t FADE_IN_DURATION = 2500;
    milliseconds_t FADE_OUT_DURATION = 5000;
    milliseconds_t MIN_EFFECT_DURATION = 2 MINUTES;
    milliseconds_t MAX_EFFECT_DURATION = 10 MINUTES;
    milliseconds_t MIN_FRAME_DURATION_MS = 0;

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

    // Un-hold: pick a new randomized transition time, keeping the current
    // effect running rather than restarting its fade-in.
    void resumeEffect();

    // Power off the LEDs and wait
    void powerOff();
    void powerOn();

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

#ifdef UNIT_TEST
    // Test-only access to private members (calcBrightness, setNextTransition,
    // the timing fields) so native unit tests can exercise them directly
    // without widening the real public API.
    friend class MissionControlTestAccess;
#endif
};
