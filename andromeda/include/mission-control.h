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

// MissionControl's own top-level mode - what a frame tick should do. Replaces
// what used to be two loosely-related bools (ON, holding): they were never
// truly independent (holding was meaningless when !ON), and adding a third
// bool for the non-blocking transition below would only make that worse.
// TRANSITIONING is the mode a rotation animation plays in: unlike the old
// blocking runRandomAnimation(), the render loop keeps ticking (and
// processing web commands) while an animation is mid-flight - see
// MissionControl::updateTransition().
enum class RenderMode : uint8_t
{
    OFF,
    FX_LOOP,
    HOLDING,
    TRANSITIONING
};

inline const char* renderModeToString(RenderMode mode)
{
    switch (mode)
    {
        case RenderMode::OFF:
            return "OFF";
        case RenderMode::FX_LOOP:
            return "FX_LOOP";
        case RenderMode::HOLDING:
            return "HOLDING";
        case RenderMode::TRANSITIONING:
            return "TRANSITIONING";
    }
    return "UNKNOWN";
}

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

    inline bool isOn() const { return mode != RenderMode::OFF; }
    inline bool isHolding() const { return mode == RenderMode::HOLDING; }

    // True when a HOLD command arrived mid-transition and is waiting for the
    // transition to land (see holdPending) - i.e. the device is committed to
    // holding but isn't there yet. Comms folds this into the wire "holding"
    // bit so the web UI's Hold button flips the instant the command is
    // accepted, instead of lagging behind for the rest of the transition.
    inline bool isHoldPending() const { return holdPending; }
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

    // False during TRANSITIONING even if the outgoing effect is a StaticColor:
    // that effect isn't on screen (the animation is) and is about to be
    // deleted by finishTransition(), so treating it as "active" here would
    // route a COLOR command to setLiveColor()/staticColor instead of
    // transitionToStaticColor() - silently writing into an effect that's
    // discarded when the transition lands instead of cancelling it.
    inline bool isColorActive() const
    {
        return mode != RenderMode::TRANSITIONING && effect && effect->wantsLiveColorUpdates();
    }

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

    // The rotation animation currently playing as a transition, or nullptr
    // when mode != TRANSITIONING. Owned by MissionControl; deleted by
    // finishTransition()/cancelTransition().
    AbstractFrameAnimation* animation = nullptr;

    // The effect to install once the in-flight TRANSITIONING finishes;
    // nullptr means "pick a random one" (the normal rotation case).
    // Mirrors handleTransition()'s nextEffect argument, just held across the
    // multiple update() ticks a transition now spans instead of being usable
    // immediately.
    AbstractEffect* pendingEffect = nullptr;

    RenderMode mode = RenderMode::FX_LOOP;

    // Mode to restore on powerOn() - powerOff() saves whatever mode it
    // preempted here (never TRANSITIONING: powerOff() cancels an in-flight
    // transition rather than trying to resume mid-animation later).
    RenderMode modeBeforeOff = RenderMode::FX_LOOP;

    // Set when a HOLD command arrives while mode == TRANSITIONING: holdEffect()
    // can't flip mode to HOLDING mid-transition without abandoning the
    // in-flight animation/effect swap, so it defers - finishTransition()
    // re-applies the hold once the transition actually lands on an effect.
    bool holdPending = false;

    // Brackets the phases of an in-flight TRANSITIONING: cut to black and
    // pause (separation from the outgoing effect) -> play the animation at
    // full brightness -> cut to black and pause again (separation from the
    // incoming effect). animationFinishedAt is 0 until the animation itself
    // reports done via renderFrame(), since its real length is data-dependent
    // (random per-animation durations) and can't be precomputed the way the
    // fixed-duration fade brackets below can.
    struct TransitionWindow
    {
        milliseconds_t start = 0;
        milliseconds_t preDelayEnd = 0;
        milliseconds_t animationFinishedAt = 0;
    };
    TransitionWindow transitionWindow;
    static constexpr milliseconds_t PRE_ANIMATION_DELAY = 200;
    static constexpr milliseconds_t POST_ANIMATION_DELAY = 200;

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

    // Drives the current phase of an in-flight TRANSITIONING (pre-delay ->
    // animation -> post-delay), then hands off to finishTransition() once the
    // whole window has elapsed. Called once per update() tick instead of
    // owning a blocking loop - see handleTransition() for how a transition
    // gets started.
    void updateTransition(milliseconds_t t);

    // Installs pendingEffect (or a random one) as the current effect, ending
    // an in-flight TRANSITIONING and returning to FX_LOOP - or re-applying a
    // deferred HOLD if one arrived mid-transition (see holdPending).
    void finishTransition();

    // Tears down an in-flight transition's animation without installing any
    // effect - used when something needs to override the transition outright
    // (a fresh handleTransition() call, or powerOff()) rather than let it run
    // to completion.
    void cancelTransition();

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

    // When the transition time is reached (or a NEXT/COLOR command asks for
    // one directly): cancels any transition already in flight, then either
    // - playAnimation == true: kicks off a new TRANSITIONING window (mode =
    //   TRANSITIONING; updateTransition()/finishTransition() take it from
    //   there across the following update() ticks), or
    // - playAnimation == false: installs nextEffect immediately, synchronously
    //   (used by transitionToStaticColor() - a web COLOR command must take
    //   effect right away, not after an animation plays).
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
