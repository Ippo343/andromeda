#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

#include <FastLED.h>
#include "utils.h"
#include "geometry.h"
#include "moodlight.h"
#include "effects.h"
#include "animations.h"

AbstractAnimation* animation;
AbstractEffect*    effect;

milliseconds MIN_EFFECT_DURATION = 10000;
milliseconds MAX_EFFECT_DURATION = 30000;
milliseconds nextTransition = 0;

void setNextTransition()
{
  nextTransition = millis() + random(MIN_EFFECT_DURATION, MAX_EFFECT_DURATION);
}

// Pick a new random animation, play it, and deallocate it
void runRandomAnimation()
{
    animation = getRandomAnimation();

    animation->run();
    animation->cleanup();
    delay(200);

    delete animation;
    animation = NULL;
}


// When the transition time is reached:
// - play an animation
// - pick a new effect
// - pick the next transition time
void handleTransition()
{
    runRandomAnimation();
    if (effect)
      delete effect;
    effect = getRandomEffect();
    setNextTransition();
}

void update(milliseconds t)
{
  if (t >= nextTransition)
    handleTransition();

  effect->precompute(t);
  effect->render(STRIPS, t);
  effect->postprocess(t);

  FastLED.show();
}

#endif