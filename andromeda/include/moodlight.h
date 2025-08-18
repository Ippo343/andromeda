#pragma once

#include <FastLED.h>
#include "utils.h"

// Moodlights are essentially sources of fluctuating colors,
// because each RGB channel is attached to a sine wave.

class MoodLight
{
  public:

    // Min period and period range
    static const byte MIN_BPM = 3;
    static const byte MAX_BPM = 20;

    // Period of each channel's wave
    // (in beats per minutes since that's what FastLED uses)
    RandParam<byte, MIN_BPM, MAX_BPM> bpmR;
    RandParam<byte, MIN_BPM, MAX_BPM> bpmG;
    RandParam<byte, MIN_BPM, MAX_BPM> bpmB;

  void randomize()
  {
    bpmR.randomize();
    bpmG.randomize();
    bpmB.randomize();
  }

  CRGB evaluate()
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
