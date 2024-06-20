#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

#include <FastLED.h>
#include "utils.h"
#include "geometry.h"
#include "moodlight.h"
#include "effects.h"
#include "animations.h"

// Set either of these to force the controller to use it
// Useful when developing a new effect or animation
AbstractAnimation* forcedAnimation = NULL;
AbstractEffect*    forcedEffect = NULL;

AbstractAnimation* animation;
AbstractEffect*    effect;

milliseconds MIN_EFFECT_DURATION = 10000;
milliseconds MAX_EFFECT_DURATION = 30000;
milliseconds nextTransition;

void setNextTransition()
{
  nextTransition = millis() + random(MIN_EFFECT_DURATION, MAX_EFFECT_DURATION);
}

// Pick a new random animation, play it, and deallocate it
// (unless it's forced, in which case it's always run and kept)
void runRandomAnimation()
{
  if (!forcedAnimation)
      animation = getRandomAnimation();

    animation->run();
    animation->cleanup();
    delay(200);

    if (!forcedAnimation)
    {
      delete animation;
      forcedAnimation = NULL;
    }
}

// Pick a new random effect, unless one is forced.
// The effect is always randomized, even if forced
void setEffect()
{
  effect = forcedEffect ? forcedEffect : getRandomEffect();
  effect->randomize();
}

// When the transition time is reached:
// - play an animation
// - pick a new effect
// - pick the next transition time
void handleTransition()
{
    runRandomAnimation();

    if (!forcedEffect)
    {
      delete effect;
      effect = NULL;
    }

    setEffect();
    setNextTransition();
}

void update(milliseconds t)
{
  if (t > nextTransition)
    handleTransition();

  effect->precompute(t);
  effect->render(STRIPS, t);
  effect->postprocess(t);

  FastLED.show();
}

#endif