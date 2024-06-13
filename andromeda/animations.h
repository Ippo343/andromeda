#ifndef ANIMATIONS_H
#define ANIMATIONS_H

#include "geometry.h"
#include "animation-base.h"

// Sweeps all the strips with RGBW (and then black) sequentially
class SweepRGBW : public AbstractAnimation
{
  public:

    byte timeStep = 20;

    void run() override {
      colorSweep(CRGB::Red);
      colorSweep(CRGB::Green);
      colorSweep(CRGB::Blue);
      colorSweep(CRGB::White);
      colorSweep(CRGB::Black);
    }

    void colorSweep(CRGB color)
    {
      FOR_EACH_LED {
        FOR_EACH_STRIP {
          STRIPS[iStrip].buffer[iLed] = color;
        }
        FastLED.show();
        delay(timeStep);
      }
    }
};

#endif