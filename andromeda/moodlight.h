#ifndef MOODLIGHT_H
#define MOODLIGHT_H

#include <FastLED.h>
#include "utils.h"

// Moodlights are essentially sources of fluctuating colors,
// because each RGB channel is attached to a sine wave.

class MoodLight
{
  public:
    // Period of each channel's wave
    // (in beats per minutes since that's what FastLED uses)
    byte bpmR;
    byte bpmG;
    byte bpmB;

    // Min period and period range
    byte MIN_BPM = 6;
    byte MAX_BPM = 30;

  void randomize()
  {
    bpmR = random8(MIN_BPM, MAX_BPM);
    bpmG = random8(MIN_BPM, MAX_BPM);
    bpmB = random8(MIN_BPM, MAX_BPM);
  }

  CRGB evaluate(int led, milliseconds t)
  {
    // NOTE: this actually ignores the t argument
    // because computing sin(t) has horrible performance
    // which also gets much worse very quickly as t increases.
    // FastLED implements integer approximations, but they get the time
    // by calling millis() internally.
    byte r = beatsin8(bpmR);
    byte g = beatsin8(bpmG);
    byte b = beatsin8(bpmB);

    return CRGB(r, g, b);
  }
};

#endif