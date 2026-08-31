#pragma once

#include <FastLED.h>

#include "utils.h"

// Moodlights are essentially sources of fluctuating colors,
// because each RGB channel is attached to a sine wave.

class MoodLight
{
   public:
    // Min period and period range
    static const uint8_t MIN_BPM = 3;
    static const uint8_t MAX_BPM = 20;

    // Period of each channel's wave
    // (in beats per minutes since that's what FastLED uses)
    RandParam<uint8_t, MIN_BPM, MAX_BPM> bpmR;
    RandParam<uint8_t, MIN_BPM, MAX_BPM> bpmG;
    RandParam<uint8_t, MIN_BPM, MAX_BPM> bpmB;

    // No separate randomize() here: bpmR/bpmG/bpmB are RandParam, whose own constructor
    // already randomizes on construction (see RandParam() in utils.h) - unlike
    // CartesianMoodlight's plain non-self-randomizing fields, there's nothing left to do.
    CRGB evaluate()
    {
        // NOTE: this actually ignores the t argument
        // because computing sin(t) has horrible performance
        // which also gets much worse very quickly as t increases.
        // FastLED implements integer approximations, but they get the time
        // by calling millis() internally.
        uint8_t r = beatsin8(bpmR);
        uint8_t g = beatsin8(bpmG);
        uint8_t b = beatsin8(bpmB);

        return CRGB(r, g, b);
    }
};
