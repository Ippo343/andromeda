#include "mission-control.h"

#include <Preferences.h>

#include "device-identity.h"
#include "nvs-utils.h"

#ifndef UNIT_TEST
#include <esp_system.h>  // esp_restart()
#endif

namespace BrightnessConfig
{
static const char* PREFS_NAMESPACE = "device";
static const char* BRIGHTNESS_KEY = "max_bright";

void persist(uint8_t value)
{
    // commit:true is sent once per slider drag by a well-behaved client (see the call site's
    // comment in comms.cpp), but it's client-controlled and unauthenticated (no auth on any
    // WS route) and this runs straight off the async_tcp task - nothing previously stopped a
    // buggy or hostile client from spamming it into a flash write per message. Skip the write
    // (and the blocking NVS round-trip that comes with it) when nothing actually changed.
    // -1 sentinel: always write the first time, regardless of what load() may or may not have
    // been called with.
    static int lastPersisted = -1;
    if (lastPersisted == value) return;

    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, false)) return;
    putUShortOrWarn(prefs, BRIGHTNESS_KEY, value);
    prefs.end();

    lastPersisted = value;
    Log.noticeln("Persisted max brightness: %d", value);
}

uint8_t load(uint8_t fallback)
{
    Preferences prefs;
    if (!beginPreferencesOrWarn(prefs, PREFS_NAMESPACE, true)) return fallback;
    uint16_t value = prefs.getUShort(BRIGHTNESS_KEY, fallback);
    prefs.end();

    // Stored as a uint16_t (see persist() above) but only ever a valid uint8_t brightness -
    // a corrupt value outside that range used to silently narrow (e.g. 256 -> 0), which looks
    // exactly like a dead device at boot. Fall back instead of truncating.
    if (value > 255)
    {
        Log.errorln("Stored brightness %d out of range, using fallback %d", value, fallback);
        return fallback;
    }

    return static_cast<uint8_t>(value);
}
}  // namespace BrightnessConfig

// Initialize the web command queue
void MissionControl::initWebQueue()
{
    webCommandQueue = xQueueCreate(WEB_QUEUE_SIZE, sizeof(Command));
    if (webCommandQueue == nullptr) { Log.errorln("Failed to create web command queue"); }
}

// Process any pending web commands
void MissionControl::processWebCommands()
{
    if (webCommandQueue == nullptr)
    {
        Log.errorln("Web command queue is not initialized");
        return;
    }

    Command command;
    while (xQueueReceive(webCommandQueue, &command, 0) == pdTRUE)
    {
        Log.noticeln("Processing web command: %s", commandTypeToString(command.type));

        switch (command.type)
        {
            case CommandType::NEXT:
                handleTransition();
                break;
            case CommandType::HOLD:
                holdEffect();
                break;
            case CommandType::RESUME:
                resumeEffect();
                break;
            case CommandType::POWER_OFF:
                powerOff();
                break;
            case CommandType::POWER_ON:
                powerOn();
                break;
            case CommandType::COLOR:
                staticColor = CRGB(command.r, command.g, command.b);
                if (!isColorActive()) transitionToStaticColor();
                stateDirty = true;
                break;
            case CommandType::MODEL:
                // Reject any id that doesn't resolve to a real registry entry (e.g. a
                // stray/garbage value, or GRID_TEST_DEVICE - a valid enum with no hardware
                // registry entry) rather than persisting it: an unresolvable model id leaves
                // config == nullptr on the next boot before WiFi is even up, bricking the
                // device with no recovery path (see Geometry::allocateAndLoadCoordinates()).
                if (getModelConfig(static_cast<ModelId>(command.modelId)))
                {
                    FactoryConfig::setModelId(static_cast<ModelId>(command.modelId));
                    stateDirty = true;
                }
                else { Log.warningln("Rejected unknown model id %d", command.modelId); }
                break;
            case CommandType::REBOOT:
                esp_restart();
                break;
            case CommandType::EFFECT:
                if (command.effectId < NUM_EFFECTS)
                {
                    handleTransition(createEffect(static_cast<EffectId>(command.effectId)));
                    holdEffect();
                }
                break;
            case CommandType::DEVICE_NAME:
                DeviceIdentity::setDeviceName(command.deviceName);
                stateDirty = true;
                break;
        }
    }
}

// Queue a web command from the web server
bool MissionControl::queueWebCommand(Command command)
{
    if (webCommandQueue == nullptr)
    {
        Log.errorln("Web command queue is not initialized");
        return false;
    }

    if (xQueueSend(webCommandQueue, &command, 0) == pdTRUE) { return true; }
    else
    {
        Log.warningln("Web command queue full, dropping command: %s",
                      commandTypeToString(command.type));

        // The web UI applies commands like NEXT/HOLD optimistically to its own local state
        // before the response comes back (see controls.js) - a silently dropped command
        // otherwise leaves it showing the wrong thing until some unrelated change happens to
        // trigger the next broadcast. Marking dirty here doesn't correct the UI on its own
        // (this device-side state hasn't actually changed), but it does force the next
        // broadcast to go out promptly instead of waiting for the periodic keepalive, so the
        // client resyncs to whatever the device is actually doing as soon as possible.
        stateDirty = true;
        return false;
    }
}

// Picks a new transition time and resets the other timestamps accordingly
void MissionControl::setNextTransition()
{
    effectStart = millis();
    fadeInEnd = effectStart + FADE_IN_DURATION;
    fadeOutStart = fadeInEnd + random(MIN_EFFECT_DURATION, MAX_EFFECT_DURATION);
    nextTransition = fadeOutStart + FADE_OUT_DURATION;

    Log.noticeln("Next transition in %lu ms", nextTransition - effectStart);
}

// Return the master brightness to fade the effects in and out
//
//   fadeInEnd   fadeOutStart
//     /¯¯¯¯¯¯¯¯¯¯¯¯¯¯\
//    /                \
// effectStart      nextTransition
//
uint8_t MissionControl::calcBrightness(milliseconds_t t)
{
    uint8_t brightness = 0;

    // Most of the time is spent in the middle so test that first
    // we are all about microseconds in this highly efficient architecture
    if (t >= fadeInEnd && t <= fadeOutStart) { brightness = this->maxBrightness; }
    // t can be older than effectStart: update()'s t is captured by the caller before
    // processWebCommands() runs, and a command handled there (e.g. NEXT) can call
    // handleTransition() synchronously, resetting effectStart to a later millis()
    // before this same update() call reaches here. Without this guard, t - effectStart
    // underflows (both are uint32_t) and produces a large bogus brightness instead of 0.
    else if (t < effectStart) { brightness = 0; }
    else if (t < fadeInEnd)
    {
        milliseconds_t dt = (t - effectStart);
        brightness = map(dt, 0, FADE_IN_DURATION, 0, this->maxBrightness);
    }
    else if (t > fadeOutStart)
    {
        milliseconds_t dt = (t - fadeOutStart);
        brightness = map(dt, 0, FADE_OUT_DURATION, this->maxBrightness, 0);
    }

    return dim8_raw(constrain(brightness, 0, 255));
}

// Tears down an in-flight transition's animation without installing any
// effect. Safe to call unconditionally (idempotent when nothing is in
// flight) - see mission-control.h for when each caller needs it.
void MissionControl::cancelTransition()
{
    if (animation)
    {
        Log.noticeln("Cancelling in-flight transition animation: %s", animation->GetName());
        delete animation;
        animation = nullptr;
    }
    // Only ever non-null for a transition that hasn't landed yet (see
    // handleTransition()) - cancelling one drops it too, or it would leak.
    if (pendingEffect)
    {
        delete pendingEffect;
        pendingEffect = nullptr;
    }
    holdPending = false;
}

// Drives one phase of an in-flight TRANSITIONING per update() tick: cut to
// black and pause -> play the animation at full brightness -> cut to black
// and pause again -> hand off to finishTransition(). Never calls
// FASTLED_SHOW() itself - update() owns that, exactly once per tick,
// regardless of which mode is active.
void MissionControl::updateTransition(milliseconds_t t)
{
    // t can be stale relative to transitionWindow.start for the same reason
    // calcBrightness() guards against t < effectStart: a command processed
    // earlier in this same update() tick (e.g. NEXT) can call
    // handleTransition(), which resets transitionWindow.start to a fresh,
    // later millis() before this point is reached.
    if (t < transitionWindow.start)
    {
        FastLED.setBrightness(0);
        return;
    }

    if (t < transitionWindow.preDelayEnd)
    {
        FastLED.setBrightness(0);
        return;
    }

    if (transitionWindow.animationFinishedAt == 0)
    {
        FastLED.setBrightness(dim8_raw(maxBrightness));
        milliseconds_t animationT = t - transitionWindow.preDelayEnd;
        if (animation->renderFrame(animationT))
        {
            // 0 doubles as "not finished yet" - see mission-control.h's
            // TransitionWindow comment. That sentinel would only misfire if
            // an animation reported done at the exact millisecond the
            // transition began, which handleTransition() already rules out
            // by construction (preDelayEnd is always > 0 by the time
            // animations can finish).
            transitionWindow.animationFinishedAt = t;
        }
        return;
    }

    if (t < transitionWindow.animationFinishedAt + POST_ANIMATION_DELAY)
    {
        FastLED.setBrightness(0);
        return;
    }

    delete animation;
    animation = nullptr;
    finishTransition();
}

// Installs pendingEffect (or a random one), ending the transition and
// returning to FX_LOOP - or HOLDING, if a HOLD command arrived mid-transition
// and was deferred (see holdPending).
void MissionControl::finishTransition()
{
    if (effect) delete effect;

    effect = pendingEffect ? pendingEffect : getRandomEffect();
    pendingEffect = nullptr;

    Log.noticeln("Picked new effect: %s", effect->GetName());

    if (effect->controlHints & ControlHints::ROTATE_SPACE)
        GEOMETRY.applyGlobalRandomRotation();
    else
        GEOMETRY.resetGlobalTransform();

    setNextTransition();
    mode = RenderMode::FX_LOOP;
    stateDirty = true;

    if (holdPending)
    {
        holdPending = false;
        holdEffect();
    }
}

// Starts a transition to a new effect: cancels one already in flight (a
// fresh NEXT/COLOR always wins over whatever was already happening), then
// either kicks off a new TRANSITIONING window (playAnimation) or installs
// nextEffect immediately (!playAnimation, e.g. transitionToStaticColor() -
// a web COLOR command must take effect right away).
void MissionControl::handleTransition(AbstractEffect* nextEffect, bool playAnimation)
{
    Log.noticeln("Handling transition");

    cancelTransition();

    if (!playAnimation)
    {
        pendingEffect = nextEffect;
        finishTransition();
        return;
    }

    animation = getRandomAnimation();
    Log.noticeln("Picked new animation: %s", animation->GetName());

    if (animation->controlHints & ControlHints::ROTATE_SPACE)
        GEOMETRY.applyGlobalRandomRotation();
    else
        GEOMETRY.resetGlobalTransform();

    pendingEffect = nextEffect;

    transitionWindow.start = millis();
    transitionWindow.preDelayEnd = transitionWindow.start + PRE_ANIMATION_DELAY;
    transitionWindow.animationFinishedAt = 0;

    mode = RenderMode::TRANSITIONING;
    stateDirty = true;
}

void MissionControl::holdEffect()
{
    // Set nextTransition to the maximum possible value,
    // so that it's never reached and the current effect is held forever.
    // You also need to set the timing of the fade out ramp to hold the brightness at max.
    // Note that using Next from the web UI resets the transition and restarts the cycle.
    nextTransition = ~0UL;
    fadeOutStart = ~0UL;

    // Can't flip to HOLDING mid-transition without abandoning the in-flight
    // animation/effect swap - defer it; finishTransition() re-applies the
    // hold once the transition actually lands on an effect. Same story if
    // there's no effect at all yet (a HOLD arriving on literally the first
    // tick, before update() has ever run a transition) - defer it the same
    // way rather than flipping straight to HOLDING with nothing to hold.
    if (mode == RenderMode::TRANSITIONING || !effect)
        holdPending = true;
    else
        mode = RenderMode::HOLDING;

    stateDirty = true;

    Log.noticeln("Holding current effect forever");
}

// Un-hold: roll a new random effect duration, but subtract however long the
// effect has already been on screen (effectStart to now, which includes the
// whole time it spent held) rather than handing it a fresh full MIN..MAX
// window on top of what it already had. effectStart/fadeInEnd are left
// untouched so the currently-running effect keeps its plateau brightness
// instead of restarting its fade-in.
void MissionControl::resumeEffect()
{
    milliseconds_t now = millis();
    milliseconds_t elapsed = now - effectStart;
    milliseconds_t duration = random(MIN_EFFECT_DURATION, MAX_EFFECT_DURATION);
    milliseconds_t remaining = (elapsed < duration) ? (duration - elapsed) : 0;

    fadeOutStart = now + remaining;
    nextTransition = fadeOutStart + FADE_OUT_DURATION;

    holdPending = false;
    if (mode != RenderMode::TRANSITIONING) mode = RenderMode::FX_LOOP;
    stateDirty = true;

    Log.noticeln("Resuming effect rotation, next transition in %lu ms", remaining);
}

void MissionControl::powerOff()
{
    Log.noticeln("Powering off (was in mode: %s)", renderModeToString(mode));

    // Immediately switch off all the lights and prevent further updates.
    // Cancels rather than tries to resume an in-flight transition later -
    // powerOn() always comes back into FX_LOOP/HOLDING, never mid-animation.
    // cancelTransition() logs its own line when it actually had an animation
    // in flight, so a power-off mid-transition shows up clearly in the log
    // as this line followed by "Cancelling in-flight transition animation".
    //
    // Computed before cancelTransition() runs (it clears holdPending): a HOLD
    // that arrived mid-transition and was deferred (holdPending) must still
    // be honored by powerOn() even though the transition itself is dropped -
    // otherwise powering off mid-transition silently loses a pending HOLD.
    if (mode != RenderMode::OFF)
        modeBeforeOff = (mode == RenderMode::TRANSITIONING)
                            ? (holdPending ? RenderMode::HOLDING : RenderMode::FX_LOOP)
                            : mode;

    cancelTransition();

    paint(CRGB::Black);
    FASTLED_SHOW();
    mode = RenderMode::OFF;
    PerformanceMonitor::Instance().stop();
    stateDirty = true;
}

void MissionControl::powerOn()
{
    mode = modeBeforeOff;
    Log.noticeln("Powering on, resuming mode: %s", renderModeToString(mode));
    stateDirty = true;
}

void MissionControl::update(milliseconds_t t)
{
    processWebCommands();

    if (mode == RenderMode::OFF)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }

    // Sync any pending web-requested color into the live effect each frame.
    // Only effects that opt in via wantsLiveColorUpdates() receive this - see
    // AbstractEffect::setColor in effects-base.h for the breadcrumb on using
    // this same seam for palette-reference colors in a future effect.
    if (effect && effect->wantsLiveColorUpdates()) effect->setColor(staticColor);

    Energy::set(slowSin(t, 0.5, 0, 255));

    milliseconds_t frameStart = t;

    if (mode == RenderMode::TRANSITIONING) { updateTransition(t); }
    else
    {
        // effect is nullptr until the first transition ever completes. A HOLD/RESUME/
        // POWER_ON command processed by processWebCommands() above on the very first tick
        // (the web server is up before the first update() call) can set nextTransition far
        // enough into the future that "t >= nextTransition" below never catches this - so
        // check for the missing effect explicitly rather than relying on that fallthrough.
        if (t >= nextTransition || !effect)
        {
            handleTransition();
            return;
        }

        FastLED.setBrightness(calcBrightness(t));

        effect->precompute(t);
        effect->render(t);
        effect->postprocess(t);
    }

    FASTLED_SHOW();

    milliseconds_t frameEnd = millis();

    // On models with no explicit cap (MIN_FRAME_DURATION_MS == 0 - Andromeda Mk1, L70, the
    // test rig) the loop previously had no unconditional yield at all: whether the idle task
    // (and with it WiFi/lwIP background work, and the CPU0 idle-task watchdog check) ever
    // got scheduled depended entirely on FASTLED_SHOW() blocking via a real task delay -
    // which isn't guaranteed for every LED driver. Floor the pacing target at 1ms so every
    // model yields at least one tick per frame regardless of its cap.
    milliseconds_t minFrameDuration = MIN_FRAME_DURATION_MS > 0 ? MIN_FRAME_DURATION_MS : 1;
    if (frameEnd - frameStart < minFrameDuration)
    {
        milliseconds_t delayTime = minFrameDuration - (frameEnd - frameStart);
        vTaskDelay(pdMS_TO_TICKS(delayTime));
    }
}

void MissionControl::transitionToStaticColor()
{
    handleTransition(new StaticColor(staticColor), false);
    holdEffect();
}
