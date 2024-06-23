#ifndef ANIMATIONS_H
#define ANIMATIONS_H

#include "geometry.h"
#include "animation-base.h"
#include "effects-utils.h"

// Sweeps all the strips with random colors,
// then white and then black sequentially
class SweepStrips : public AbstractAnimation
{
  public:

    RandParam<byte, 10, 30> timeStep;

    void run() override {
      CRGB colors[3];
      threeRandomColors(&colors[0], &colors[1], &colors[2]);

      paint(CRGB::Black);
      for (byte c = 0; c < 3; c++)
        colorSweep(colors[c]);
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


// Alternates Y shapes around the mirror showing all colors
// TODO: improve name of this
class RotateRGBW : public AbstractAnimation
{
  public:
    void run() override
    {
      paint(CRGB::Black);
      paintStrip(0, CRGB::Red);
      paintStrip(1, CRGB::Red);
      FastLED.show();
      delay(500);

      paint(CRGB::Black);
      paintStrip(0, CRGB::Green);
      paintStrip(3, CRGB::Green);
      FastLED.show();
      delay(500);

      paint(CRGB::Black);
      paintStrip(0, CRGB::Blue);
      paintStrip(5, CRGB::Blue);
      FastLED.show();
      delay(500);

      paint(CRGB::White);
      FastLED.show();
      delay(500);
    }
};


class SequentialIgnition : public AbstractAnimation
{
  public:
    void run() override
    {
      paint(CRGB::Black);

      FOR_EACH_STRIP {
        paintStrip(iStrip, randomColor());
        FastLED.show();
        delay(200);
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

  byte ANIMATIONS_COUNT = 3;

  // Set this to the index of the animation you want to force while testing
  short forcedSelection = -1;

  static byte previousSelection = 255;

  byte selection;
  if (forcedSelection >= 0)
    selection = forcedSelection;
  else do
    selection = random(ANIMATIONS_COUNT);
  while (selection == previousSelection);

  previousSelection = selection;

  switch (selection)
  {
    case 0:
      return new SweepStrips();
    case 1:
      return new RotateRGBW();
    case 2:
      return new SequentialIgnition();
    default:
      return new ErrorAnimation();
  }
}

#endif