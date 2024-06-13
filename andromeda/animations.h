#ifndef ANIMATIONS_H
#define ANIMATIONS_H

#include "geometry.h"
#include "animation-base.h"
#include "effects-utils.h"

// Sweeps all the strips with RGBW (and then black) sequentially
class SweepRGBW : public AbstractAnimation
{
  public:

    byte timeStep = 10;

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


// I don't think this will ever show, but why not
class ErrorAnimation : public AbstractAnimation
{
  public:
    void run() override
    {
      for (byte i = 0; i < 3; i++)
      {
        paint(CRGB::Red);
        FastLED.show();
        delay(500);
        paint(CRGB::Black);
        FastLED.show();
        delay(500);
      }
    }
};


AbstractAnimation* getRandomAnimation() {

  // You cannot have an array of types on this thing.
  // I have a prototype where I had an array of template functions
  // that would instantiate each effect, but unsurprisingly it crashes.
  // For the moment, KISS will do.
  // TODO: array of template functions because I can.

  byte ANIMATIONS_COUNT = 1;
  byte selection = analogRead(0) % ANIMATIONS_COUNT;

  switch (selection)
  {
    case 0:
      return new SweepRGBW();
    default:
      return new ErrorAnimation();
  }
}

#endif