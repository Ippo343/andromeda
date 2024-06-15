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

    const milliseconds MIN_STEP = 25;
    const milliseconds MAX_STEP = 50;
    milliseconds step;

    byte idxOn;

    CRGB color[NUM_STRIPS];

    void randomize() override
    {
      step = random(MIN_STEP, MAX_STEP);

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
    byte MIN_CHANCE = 1;
    byte MAX_CHANCE = 3;
    byte sparkChance;

    // Chance that the spark is bigger than usual.
    // These are out of 100 (they control the size of the spark, not the frequency)
    // and rolled sequentially (sparks have a chance to be big, big sparks have a chance to be bigger)
    byte BIG_SPARK_CHANCE = 15;
    byte REALLY_BIG_SPARK_CHANCE = 10;

    ElectricSparks()
    {
      memset(preValues, 0, NUM_STRIPS * LEDS_PER_STRIP);
      memset(newValues, 0, NUM_STRIPS * LEDS_PER_STRIP);

      palette = blue_sparks_gp;
    }

    byte avg38(byte a, byte b, byte c)
    {
      int newV = a + b + c; // to avoid overflow
      return newV / 3;
    }

    void randomize() override
    {
      sparkChance = random(MIN_CHANCE, MAX_CHANCE);
      byte selection = random(4);

      switch(selection)
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
        newValues[strip.idx][led.idx] = 255;

        if (random(100) < BIG_SPARK_CHANCE)
        {
          newValues[strip.idx][LI(led.idx + 1)] = 255;
          newValues[strip.idx][LI(led.idx - 1)] = 255;

          if (random(100) < REALLY_BIG_SPARK_CHANCE)
          {
            newValues[strip.idx][LI(led.idx + 2)] = 255;
            newValues[strip.idx][LI(led.idx - 2)] = 255;
          }
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
      memcpy(preValues, newValues, NUM_STRIPS * LEDS_PER_STRIP);
    }
};


// Whole mirror moodlight with pulsating brightness
class Glow : public AbstractEffect
{
  public:
    // TODO: randomize
    byte MIN_BPM = 6;
    byte MAX_BPM = 15;
    byte bpm;

    byte MIN_BRIGHTNESS = 50;
    byte MAX_BRIGHTNESS = 255;

    CRGB color;
    MoodLight moodlight;

    void randomize() override
    {
      // Use a very very slow moodlight
      moodlight.MIN_BPM = 1;
      moodlight.MAX_BPM = 3;
      moodlight.randomize();

      bpm = random(MIN_BPM, MAX_BPM);
    }

    void precompute(milliseconds t) override
    {
      color = moodlight.evaluate(0, t);
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      return color;
    }

    void postprocess(milliseconds t) override
    {
      byte brightness = beatsin8(bpm, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
      FastLED.setBrightness(brightness);
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
    byte MIN_CHANCE = 30;
    byte MAX_CHANCE = 50;
    byte sparkChance;

    void randomize() override
    {
      sparkChance = random(MIN_CHANCE, MAX_CHANCE);
    }

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


AbstractEffect* getRandomEffect() {

  // You cannot have an array of types on this thing.
  // I have a prototype where I had an array of template functions
  // that would instantiate each effect, but unsurprisingly it crashes.
  // For the moment, KISS will do.
  // TODO: array of template functions because I can.

  byte EFFECTS_COUNT = 6;
  byte selection = random(EFFECTS_COUNT);

  switch (selection)
  {
    case 0:
      return new IndividualStripMoodlight();
    case 1:
      return new LoopingPoint();
    case 2:
      return new ElectricSparks();
    case 3:
      return new Glow();
    case 4:
      return new Fireworks();
    case 5:
      return new PerlinColorMoodlight();
    default:
      return new ErrorEffect();
  }
}

#endif