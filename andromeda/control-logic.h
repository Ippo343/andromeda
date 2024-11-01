#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

#include <FastLED.h>

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"
#include "control-hints.h"
#include "effects.h"
#include "animations.h"

AbstractAnimation* animation;
AbstractEffect*    effect;

// These parameters control how long an effect lasts and how quickly it fades in and out
milliseconds FADE_IN_DURATION  = 2500;
milliseconds FADE_OUT_DURATION = 5000;
milliseconds MIN_EFFECT_DURATION = 1 MINUTES;
milliseconds MAX_EFFECT_DURATION = 5 MINUTES;

// These contain the actual timestamps to plan the fade in/out and the transition to the next effect
milliseconds effectStart = 0;       // time when the current effect started
milliseconds nextTransition = 0;    // time when the current effect will end
milliseconds fadeInEnd;             // time when the fade in will end ( = effectStart + FADE_IN_DURATION)
milliseconds fadeOutStart;          // time when the fade out will start ( = nextTransition - FADE_OUT_DURATION)


// Picks a new transition time and resets the other timestamps accordingly
void setNextTransition()
{
  effectStart = millis();
  fadeInEnd = effectStart + FADE_IN_DURATION;
  fadeOutStart = fadeInEnd + random(MIN_EFFECT_DURATION, MAX_EFFECT_DURATION);
  nextTransition = fadeOutStart + FADE_OUT_DURATION;
}

// Return the master brightness to fade the effects in and out
//
//   fadeInEnd   fadeOutStart
//     /¯¯¯¯¯¯¯¯¯¯¯¯¯¯\
//    /                \
// effectStart      nextTransition
//
byte getBrightness(milliseconds t)
{
  byte brightness = 0;

  // Most of the time is spent in the middle so test that first
  // we are all about microseconds in this highly efficient architecture
  if (t >= fadeInEnd && t <= fadeOutStart)
  {
    brightness = 255;
  }
  else if (t < fadeInEnd)
  {
    milliseconds dt = (t - effectStart);
    brightness = map(dt, 0, FADE_IN_DURATION, 0, 255);
  }
  else if (t > fadeOutStart)
  {
    milliseconds dt = (t - fadeOutStart);
    brightness = map(dt, 0, FADE_OUT_DURATION, 255, 0);
  }

  return constrain(brightness, 0, 255);
}

// Pick a new random animation, play it, and deallocate it
void runRandomAnimation()
{
    animation = getRandomAnimation();

    if (animation->controlHints & ControlHints::ROTATE_SPACE)
        applyRandomRotation();

    // First fade everything out to black and add a small delay
    // to create some separation from the effect
    FastLED.setBrightness(0);
    delay(200);

    // Reset the brightness to max and give control back to the animation
    FastLED.setBrightness(255);
    animation->run();
    animation->cleanup();

    // Turn it down to zero and wait a little bit.
    // This creates another small separation before the effect.
    // The brightness must start from zero to avoid ugly jumps when the lerp kicks in
    FastLED.setBrightness(0);
    delay(200);

    delete animation;
    animation = NULL;
}

// When the transition time is reached:
// - play an animation
// - pick a new effect
// - pick the next transition time
// - and also sprinkle random rotation transforms here and there
void handleTransition()
{
  runRandomAnimation();

  if (effect)
    delete effect;

  effect = getRandomEffect();

  if (effect->controlHints & ControlHints::ROTATE_SPACE)
    applyRandomRotation();

  setNextTransition();
}

void update(milliseconds t)
{
  if (t >= nextTransition)
  {
    handleTransition();
    return;
  }

  FastLED.setBrightness(getBrightness(t));

  effect->precompute(t);
  effect->render(STRIPS, t);
  effect->postprocess(t);

  FastLED.show();
}

#endif