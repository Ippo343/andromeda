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


// Pretty much the same as SweepStrips, but using all the loops instead
class SweepLoops : public AbstractAnimation
{
  public:

    RandParam<byte, 50, 100> timeStep;

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
        FOR_EACH_STRIP {
          paintStrip(iStrip, color);
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


// Swipes a random color from left to right and then fades it out
// TODO: swipe in random directions
class Swipe : public AbstractAnimation
{
  public:
    void run() override
    {
      paint(CRGB::Black);
      CRGB color = randomColor();

      // Even without any delay, scrolling the whole screen size takes a long time
      // (which I am a bit suspicious of to be honest, but I guess calling it 520 times is a bit much).
      // It looks smoother if you increase in steps of 2 or 3
      RandParam<short, 2, 3> step;

      // It goes to (step * SCREEN_HALF_SIZE) so that the fading trail has time to fully fade out
      for (short x = -SCREEN_HALF_SIZE; x <= (step * SCREEN_HALF_SIZE); x += step)
      {
        FOR_EACH_STRIP {
          FOR_EACH_LED {
            short lx = STRIPS[iStrip].leds[iLed].cartesian.x;
            if (lx >= (x - step) && lx <= x)
              STRIPS[iStrip].buffer[iLed] = color;
          }
          fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, step);
        }
        FastLED.show();
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

  byte ANIMATIONS_COUNT = 5;

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
    case 3:
      return new SweepLoops();
    case 4:
      return new Swipe();
    default:
      return new ErrorAnimation();
  }
}

#endif