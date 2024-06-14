#include <FastLED.h>

#define PERF

#include "utils.h"
#include "geometry.h"
#include "moodlight.h"
#include "effects.h"
#include "animations.h"

AbstractAnimation* animation;
AbstractEffect*    effect;

// Set either of these to force the controller to use it
// Useful when developing a new effect or animation
AbstractAnimation* forcedAnimation = NULL;
AbstractEffect*    forcedEffect = NULL;

void setup() {

  Serial.begin(115200);

  initializeGeometry();
  seedRNGs();

  // TODO: these need to go
  for (byte i = 0; i < NUM_STRIPS; i++) {
    moodlights[i].randomize();
  }

  if (forcedAnimation == NULL)
    animation = getRandomAnimation();
  else
    animation = forcedAnimation;

  animation->run();
  animation->cleanup();
  delay(250);
  
  delete animation;

  if (forcedEffect == NULL)
    effect = getRandomEffect();
  else
    effect = forcedEffect;
}

void loop() {

  unsigned long t = millis();

  effect->precompute(t);
  effect->render(STRIPS, t);
  effect->postprocess(t);

  FastLED.show();

  unsigned long end = millis();

#ifdef PERF
  float fps = 1000.0 / (float)(end - t);
  Serial.println(fps);
#endif
}