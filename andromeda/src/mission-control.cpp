#include "mission-control.h"

#include <Preferences.h>

#ifndef UNIT_TEST
#include <esp_system.h>  // esp_restart()
#endif

namespace BrightnessConfig
{
static const char* PREFS_NAMESPACE = "device";
static const char* BRIGHTNESS_KEY = "max_bright";

void persist(uint8_t value)
{
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUShort(BRIGHTNESS_KEY, value);
    prefs.end();

    Log.noticeln("Persisted max brightness: %d", value);
}

uint8_t load(uint8_t fallback)
{
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);  // read-only
    uint16_t value = prefs.getUShort(BRIGHTNESS_KEY, fallback);
    prefs.end();

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
                FactoryConfig::setModelId(static_cast<ModelId>(command.modelId));
                stateDirty = true;
                break;
            case CommandType::REBOOT:
                esp_restart();
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

    Log.noticeln("Next transition in %d ms", nextTransition);
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

// Pick a new random animation, play it, and deallocate it.
//
// TEMPORARY: animations were converted to AbstractFrameAnimation's per-frame
// renderFrame(t) interface (renders one frame, no delay()/FASTLED_SHOW() of
// its own), but this method still drives that interface with a blocking spin
// loop, so from the outside it behaves exactly as before - the actual
// non-blocking integration (driving renderFrame() once per MissionControl::
// update() tick, so processWebCommands() keeps running during a transition)
// lands in the very next commit, which removes this loop entirely.
void MissionControl::runRandomAnimation()
{
    AbstractFrameAnimation* animation = getRandomAnimation();

    Log.noticeln("Picked new animation: %s", animation->GetName());

    if (animation->controlHints & ControlHints::ROTATE_SPACE)
        GEOMETRY.applyGlobalRandomRotation();
    else
        GEOMETRY.resetGlobalTransform();

    // First fade everything out to black and add a small delay
    // to create some separation from the effect
    FastLED.setBrightness(0);
    FASTLED_SHOW();
    delay(200);

    // Reset the brightness to max and give control back to the animation
    FastLED.setBrightness(MissionControl::Instance().getMaxBrightness());
    milliseconds_t animationStart = millis();
    bool done = false;
    while (!done)
    {
        done = animation->renderFrame(millis() - animationStart);
        FASTLED_SHOW();
    }
    paint(CRGB::Black);
    FASTLED_SHOW();

    // Turn it down to zero and wait a little bit.
    // This creates another small separation before the effect.
    // The brightness must start from zero to avoid ugly jumps when the lerp kicks in
    // setBrightness() only takes effect on the next show(), so push it now or the
    // strip keeps displaying the animation's last (bright) frame during the delay.
    FastLED.setBrightness(0);
    FASTLED_SHOW();
    delay(200);

    delete animation;
    animation = NULL;
}

// When the transition time is reached:
// - play an animation
// - pick a new effect (unless one is passed in)
// - pick the next transition time
// - and also sprinkle random rotation transforms here and there
void MissionControl::handleTransition(AbstractEffect* nextEffect, bool playAnimation)
{
    Log.noticeln("Handling transition");

    ON = true;        // Ensure the system is ON
    holding = false;  // A transition (e.g. NEXT) always leaves hold mode

    if (playAnimation) runRandomAnimation();

    // Delete the current effect if it exists
    if (effect) delete effect;

    // If a next effect is provided, use it, otherwise pick a new random effect
    if (nextEffect)
        effect = nextEffect;
    else
        effect = getRandomEffect();

    Log.noticeln("Picked new effect: %s", effect->GetName());

    if (effect->controlHints & ControlHints::ROTATE_SPACE)
        GEOMETRY.applyGlobalRandomRotation();
    else
        GEOMETRY.resetGlobalTransform();

    setNextTransition();
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
    holding = true;
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
    holding = false;
    stateDirty = true;

    Log.noticeln("Resuming effect rotation, next transition in %d ms", remaining);
}

void MissionControl::powerOff()
{
    // Immediately switch off all the lights and prevent further updates
    paint(CRGB::Black);
    FASTLED_SHOW();
    ON = false;
    PerformanceMonitor::Instance().stop();
    stateDirty = true;
}

void MissionControl::powerOn()
{
    ON = true;
    stateDirty = true;
}

void MissionControl::update(milliseconds_t t)
{
    processWebCommands();

    if (!ON)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        return;
    }

    // Sync any pending web-requested color into the live effect each frame.
    // Only effects that opt in via wantsLiveColorUpdates() receive this - see
    // AbstractEffect::setColor in effects-base.h for the breadcrumb on using
    // this same seam for palette-reference colors in a future effect.
    if (effect && effect->wantsLiveColorUpdates()) effect->setColor(staticColor);

    Energy::set(slowSin(millis(), 0.5, 0, 255));

    if (t >= nextTransition)
    {
        handleTransition();
        return;
    }

    milliseconds_t frameStart = millis();

    FastLED.setBrightness(calcBrightness(t));

    effect->precompute(t);
    effect->render(t);
    effect->postprocess(t);

    FASTLED_SHOW();

    milliseconds_t frameEnd = millis();

    if (frameEnd - frameStart < MIN_FRAME_DURATION_MS)
    {
        milliseconds_t delayTime = MIN_FRAME_DURATION_MS - (frameEnd - frameStart);
        vTaskDelay(pdMS_TO_TICKS(delayTime));
    }
}

void MissionControl::transitionToStaticColor()
{
    handleTransition(new StaticColor(staticColor), false);
    holdEffect();
}
