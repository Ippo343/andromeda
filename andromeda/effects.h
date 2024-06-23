#ifndef EFFECTS_H
#define EFFECTS_H

#include <FastLED.h>
#include "geometry.h"
#include "utils.h"
#include "effects-base.h"
#include "effects-utils.h"
#include "color-palettes.h"

// Use each individual led strip as an independent moodlight.
// All leds in the same strip have the same color,
// but each strip fluctuates independently
class IndividualStripMoodlight : public AbstractEffect
{
  public:
    MoodLight moodlights[NUM_STRIPS];
    CRGB colors[NUM_STRIPS];

    void randomize() override
    {
      FOR_EACH_STRIP {
        moodlights[iStrip].randomize();
      }
    }

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {
        colors[iStrip] = moodlights[iStrip].evaluate(0, t);
      }
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      return colors[strip.idx];
    }
};


// One single led turned on per strip,
// looping around it very fast
class LoopingPoint : public AbstractEffect
{
  public:
    MoodLight moodlights[NUM_STRIPS];
    RandParam<milliseconds, 25, 50> step;
    byte idxOn;

    CRGB color[NUM_STRIPS];

    void randomize() override
    {
      FOR_EACH_STRIP {
        moodlights[iStrip].randomize();
      }
    }

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {
        color[iStrip] = moodlights[iStrip].evaluate(0, t);
      }

      // Pick an LED to turn on, rotating along the strip
      idxOn = (int)(t / step) % (LEDS_PER_STRIP);
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      // If this is the led to turn on, apply the color;
      // otherwise, leave it unchanged.
      // They will be faded away in postprocessing
      return led.idx == idxOn
        ? color[strip.idx]
        : strip.buffer[led.idx];
    }

    void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, 25);
      }
    }
};


// Lights up the whole mirror blue, and randomly adds sparks of white
// that die out very quickly diffusing to neighbouring leds
class ElectricSparks : public AbstractEffect
{
  public:
    // Palette values for each LED
    // Needs double buffering to correctly compute the averaging of neighbouring pixels
    byte preValues[NUM_STRIPS][LEDS_PER_STRIP];
    byte newValues[NUM_STRIPS][LEDS_PER_STRIP];

    CRGBPalette16 palette;

    // This is tricky to figure out if not by trial and error.
    // We roll a dice every frame for every led, so the chance must be really small.
    // These are values that I like experimentally, I cannot justify them.
    // TODO: better way to define the frequency
    unsigned short DICE_LIMIT = 10000;
    RandParam<byte, 1, 3> sparkChance;

    // Chance that a spark becomes bigger, rolled out of 100.
    // If the roll is successful, the width is doubled and then rolled again until it fails.
    // Potentially going up to the full strip in rare cases.
    RandParam<byte, 10, 30> bigSparkChance;

    RandParam<byte, 0, 3> paletteSelection;

    ElectricSparks()
    {
      memset8(preValues, 0, NUM_STRIPS * LEDS_PER_STRIP);
      memset8(newValues, 0, NUM_STRIPS * LEDS_PER_STRIP);

      palette = blue_sparks_gp;
    }

    byte avg38(byte a, byte b, byte c)
    {
      int newV = a + b + c; // to avoid overflow
      return newV / 3;
    }

    void randomize() override
    {
      switch(paletteSelection)
      {
        case 0:
          palette = red_sparks_gp;
          break;
        case 1:
          palette = green_sparks_gp;
          break;
        case 2:
          palette = blue_sparks_gp;
          break;
        case 3:
          palette = purple_sparks_gp;
          break;
      }
    }

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {

        // Value diffusion between neighbouring LEDs:
        // each LED becomes the average of its neighbours (poor man's heat conduction),
        // taking the value from the previous buffer so that the new buffer is computed correctly
        // TODO: unsurprisingly it sucks. It dissipates way too fast.
        // TODO: solve the heat conduction partial differential equation

        for (byte iLed = 0; iLed < LEDS_PER_STRIP; iLed++)
          newValues[iStrip][iLed] = avg38(
            preValues[iStrip][LI(iLed - 1)],
            preValues[iStrip][iLed],
            preValues[iStrip][LI(iLed + 1)]);
      }
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      // Random injection of new spikes
      if (random(DICE_LIMIT) < sparkChance)
      {
        byte width = 1;

        // Keep rolling for a chance to increase the spark's size
        while (random(100) < bigSparkChance)
          width *= 2;

        // Now light up the pixel and its neighbours up to the defined width
        for (byte w = 0; w < width; w++)
        {
          newValues[strip.idx][LI(led.idx + w)] = 255;
          newValues[strip.idx][LI(led.idx - w)] = 255;
        }
      }

      return ColorFromPalette(palette, preValues[strip.idx][led.idx]);
    }

    void postprocess(milliseconds t) override
    {
      // Dissipate the energy to lower values
      FOR_EACH_STRIP {
        FOR_EACH_LED {
          newValues[iStrip][iLed] = scale8(newValues[iStrip][iLed], 254);
        }
      }

      // Copy the current buffer so that the next frame can diffuse it
      memcpy8(preValues, newValues, NUM_STRIPS * LEDS_PER_STRIP);
    }
};


// Whole mirror moodlight pulsating around a central hue
class Glow : public AbstractEffect
{
  public:
    RandParam<milliseconds, 10000, 30000> cycleTime;
    RandParam<byte, 0, 255> hueCentre;
    RandParam<byte, 5, 25> hueAmplitude;
    CRGB color;

    void precompute(milliseconds t) override
    {
      long scaledWave = scaledCubicWave8(t, cycleTime, -hueAmplitude, hueAmplitude);
      byte hue = (hueCentre + scaledWave) % 255;
      color = CHSV(hue, 255, 255);
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      return color;
    }
};


class VerticalPaletteWave : public AbstractEffect
{
  public:
    CRGBPalette16 palette = HeatColors_p;
    byte bpm = 10;

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      byte value = beatsin8(bpm, 0, 255, 0, -led.cartesian.y / 3);
      return ColorFromPalette(palette, value);
    }
};

// Randomly light up a whole strip with a random color,
// and then keep everything fading to black.
// Looks a little bit like fireworks.
class Fireworks: public AbstractEffect
{
  public:
    // See ElectricSpark's comments, same logic
    unsigned long DICE_LIMIT = 10000;
    RandParam<byte, 30, 50> sparkChance;

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {
        // Randomly fill the whole strip with a random color
        if (random(DICE_LIMIT) < sparkChance)
          paintStrip(iStrip, randomColor());
      }
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      return STRIPS[strip.idx].buffer[led.idx];
    }

    void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, 5);
      }
    }
};


// A whole-mirror moodlight where the color is chosen
// as perlin noise applied to H and S of HSV space
class PerlinColorMoodlight : public AbstractEffect
{
  public:
    byte colorValue;
    byte saturationValue;
    byte scale = 20;
    CRGB color;

    void precompute(milliseconds t) override
    {
      byte x = t / scale;
      colorValue = inoise8(x);
      saturationValue = inoise8(x);
      color = CHSV(colorValue, saturationValue, 255);
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      return color;
    }
};

// I don't think this will ever show, but why not
class ErrorEffect : public AbstractEffect
{
  public:
    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      if (strip.idx == 0)
        return CRGB::Red;
      else
        return CRGB::Black;
    }
};


// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect() {

  byte EFFECTS_COUNT = 7;

  // Set this to the index of the effect you want to force while testing
  short forcedSelection = -1;

  static byte previousSelection = 255;

  byte selection;
  if (forcedSelection >= 0)
    selection = forcedSelection;
  else do
    selection = random(EFFECTS_COUNT);
  while (selection == previousSelection);

  previousSelection = selection;

  AbstractEffect* retval;
  switch (selection)
  {
    case 0:
      retval = new IndividualStripMoodlight();
      break;
    case 1:
      retval = new LoopingPoint();
      break;
    case 2:
      retval = new ElectricSparks();
      break;
    case 3:
      retval = new Glow();
      break;
    case 4:
      retval = new Fireworks();
      break;
    case 5:
      retval = new PerlinColorMoodlight();
      break;
    case 6:
      retval = new VerticalPaletteWave();
      break;
    default:
      retval = new ErrorEffect();
      break;
  }

  retval->randomize();
  return retval;
}

#endif
