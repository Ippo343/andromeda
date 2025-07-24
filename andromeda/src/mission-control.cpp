#include "mission-control.h"

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
byte MissionControl::getBrightness(milliseconds t)
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

  return dim8_raw(constrain(brightness, 0, 255));
}

// Pick a new random animation, play it, and deallocate it
void MissionControl::runRandomAnimation()
{
  AbstractAnimation* animation = getRandomAnimation();

  Log.noticeln("Picked new animation: %s", animation->GetName());

  if (animation->controlHints & ControlHints::ROTATE_SPACE)
    applyGlobalRandomRotation();
  else
    resetGlobalTransform();

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
void MissionControl::handleTransition()
{
  Log.noticeln("Handling transition");

  runRandomAnimation();

  if (effect)
    delete effect;

  effect = getRandomEffect();
  Log.noticeln("Picked new effect: %s", effect->GetName());

  if (effect->controlHints & ControlHints::ROTATE_SPACE)
    applyGlobalRandomRotation();
  else
    resetGlobalTransform();

  setNextTransition();
}

void MissionControl::holdEffect()
{
  // Set nextTransition to the maximum possible value,
  // so that it's never reached and the current effect is held forever.
  // You also need to set the timing of the fade ramps to hold the brightness at max.
  // Note that using Next from the web UI resets the transition and restarts the cycle.
  nextTransition = ~0UL;
  fadeInEnd      =  0;
  fadeOutStart   = ~0UL;
  Log.noticeln("Holding current effect forever");
}

void MissionControl::powerOff()
{
  // Immediately switch off all the lights and prevent further updates
  paint(CRGB::Black);
  FastLED.show();
  ON = false;
}

void MissionControl::powerOn()
{
  // Re-eanble further update and set the next transition to happen immediately.
  // This forces the cycle to restart as soon as the lights come on,
  // so we restart with a new animation and not in the middle of whatever was interrupted
  // by the power off command
  ON = true;
  nextTransition = millis();
}

void MissionControl::update(milliseconds t)
{
  if (!ON)
    return;

  Energy::set(slowSin(millis(), 0.5, 0, 255));

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