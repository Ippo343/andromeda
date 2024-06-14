#ifndef EFFECTS_H
#define EFFECTS_H

#include <FastLED.h>
#include "geometry.h"
#include "utils.h"
#include "effects-base.h"
#include "effects-utils.h"

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
    milliseconds step = 30;
    byte idxOn;

    CRGB color[NUM_STRIPS];

    void randomize() override
    {
      FOR_EACH_STRIP {
        moodlights[iStrip].randomize();
      }
    }

    virtual void precompute(milliseconds t) override
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

    virtual void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, 25);
      }
    }
};


DEFINE_GRADIENT_PALETTE (electric_spark_gp) {
    0,   0,   0,  50, // dark blue
  100,   0,   0, 255, // full blue
  200, 100, 100, 255, // bright blue
  255, 255, 255, 255  // white
};

CRGBPalette16 electric_spark_map = electric_spark_gp;

// Lights up the whole mirror blue, and randomly adds sparks of white
// that die out very quickly diffusing to neighbouring leds
class ElectricSparks : public AbstractEffect
{
  public:
    // Palette values for each LED
    // Needs double buffering to correctly compute the averaging of neighbouring pixels
    byte preValues[NUM_STRIPS][LEDS_PER_STRIP];
    byte newValues[NUM_STRIPS][LEDS_PER_STRIP];

    ElectricSparks()
    {
      memset(preValues, 0, NUM_STRIPS * LEDS_PER_STRIP);
      memset(newValues, 0, NUM_STRIPS * LEDS_PER_STRIP);
    }

    byte avg38(byte a, byte b, byte c)
    {
      int newV = a + b + c; // to avoid overflow
      return newV / 3;
    }

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {

        // Value diffusion between neighbouring LEDs:
        // each LED becomes the average of its neighbours (poor man's heat conduction),
        // taking the value from the previous buffer so that the new buffer is computed correctly
        // TODO: unsurprisingly it sucks. It dissipates way too fast.
        // TODO: solve the heat conduction partial differential equation

        // Handle head and tail individually for obvious memory reasons
        newValues[iStrip][0] = avg38(preValues[iStrip][LEDS_PER_STRIP - 1], preValues[iStrip][0],  preValues[iStrip][1]);
        newValues[iStrip][LEDS_PER_STRIP - 1] =
          avg38(preValues[iStrip][LEDS_PER_STRIP - 2], preValues[iStrip][LEDS_PER_STRIP - 1], preValues[iStrip][0]);

        for (byte iLed = 1; iLed < LEDS_PER_STRIP - 1; iLed++)
          newValues[iStrip][iLed] = avg38(preValues[iStrip][iLed - 1], preValues[iStrip][iLed], preValues[iStrip][iLed + 1]);
      }
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      // Random injection of new spikes
      if (random(10000) < 15)
        newValues[strip.idx][led.idx] = 255;

      return ColorFromPalette(electric_spark_map, preValues[strip.idx][led.idx]);
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


class Glow : public AbstractEffect
{
  public:
    // TODO: randomize
    byte bpm = 12;
    CRGB color = CRGB(255, 220, 50);

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      return color;
    }

    void postprocess(milliseconds t) override
    {
      byte brightness = beatsin8(bpm, 128, 255);
      FastLED.setBrightness(brightness);
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

  byte EFFECTS_COUNT = 4;
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
    default:
      return new ErrorEffect();
  }
}

#endif