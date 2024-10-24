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

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {
        colors[iStrip] = moodlights[iStrip].evaluate();
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
    RandParam<milliseconds, 40, 70> step;
    byte idxOn;

    CRGB color[NUM_STRIPS];

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {
        color[iStrip] = moodlights[iStrip].evaluate();
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

    CRGBPalette256 palette;

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
    }

    byte avg38(byte a, byte b, byte c)
    {
      int newV = a + b + c; // to avoid overflow
      return newV / 3;
    }

    void randomize() override
    {
      CRGBPalette16 palette16;

      switch(paletteSelection)
      {
        case 0:
          palette16 = red_sparks_gp;
          break;
        case 1:
          palette16 = green_sparks_gp;
          break;
        case 2:
          palette16 = blue_sparks_gp;
          break;
        case 3:
          palette16 = purple_sparks_gp;
          break;
      }

      // Upscale the palette so no interpolation is needed while running.
      // gives a completely imperceptible performace boost.
      UpscalePalette(palette16, palette);
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
    RandParam<milliseconds, (2 MINUTES), (4 MINUTES)> cycleTime;
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


class PaletteWave : public AbstractEffect
{
  public:
    CRGBPalette256 palette;
    RandParam<byte, 1, 5> bpm;
    RandParam<char, -3, 3> mx;
    RandParam<char, -3, 3> my;
    RandParam<byte, 1, 5> baseScale;
    byte scale;

    PaletteWave()
    {
      // If both coefficients are 0 then all the LEDs take the same color,
      // prevent that case by rerolling
      while (mx == 0 && my == 0)
      {
        mx.randomize();
        my.randomize();
      }

      // Compensate for the magnitude of the (mx, my) vector
      // since the coordinates are effectively scaled by it.
      scale = baseScale * sqrt(mx * mx + my * my);
    }

    void randomize() override
    {
      CRGBPalette16 palette16 = randomPredefinedPalette();
      UpscalePalette(palette16, palette);
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      int v = (mx * led.cartesian.x + my * led.cartesian.y) / scale;
      byte value = beatsin8(bpm, 0, 255, 0, v);
      return ColorFromPalette(palette, value);
    }
};


// Just like the PaletteWave effect, but in polar coordinates
class PolarPaletteWave : public AbstractEffect
{
  public:
    CRGBPalette256 palette;
    RandParam<byte, 1, 5> bpm;
    RandParam<unsigned short, 1, 10> scale;
    RandParam<byte, 0, 1> flip;

    void randomize() override
    {
      CRGBPalette16 palette16 = randomPredefinedPalette();
      UpscalePalette(palette16, palette);
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      short v = (flip ? -1 : 1) * led.polar.radius / scale;
      byte value = beatsin8(bpm, 0, 255, 0, v);
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


// Rotating beam of light
class Lighthouse : public AbstractEffect
{
  public:
    unsigned short angle;
    unsigned short minAngle;
    unsigned short maxAngle;
    MoodLight moodlight;
    CRGB color;

    RandParam<byte, 3, 10> bpm;
    RandParam<unsigned short, 1200, 4500> aperture;

    void precompute(milliseconds t) override
    {
      color = moodlight.evaluate();

      unsigned short v = beat16(bpm);
      angle = map(v, 0, 65535, 0, 36000);

      minAngle = (angle - aperture) % FULL_CIRCLE;
      maxAngle = (angle + aperture) % FULL_CIRCLE;
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      bool on;

      if (minAngle > maxAngle)
        on = (led.polar.cdegrees >= minAngle || led.polar.cdegrees <= maxAngle);
      else
        on = (led.polar.cdegrees >= minAngle && led.polar.cdegrees <= maxAngle);

      if (on)
        return color;
      else
        return strip.buffer[led.idx];
    }

    void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, 5);
      }
    }
};


class PolarSwipe : public AbstractEffect
{
  public:
    unsigned short radius;
    unsigned short minRadius;
    unsigned short maxRadius;
    CRGB color;
    RandParam<byte, 0, 1> flip;

    RandParam<byte, 6, 20> bpm;
    RandParam<byte, 5, 30> aperture;

    void randomize() override
    {
      color = randomColor();
    }

    void precompute(milliseconds t) override
    {
      unsigned short v = beat16(bpm);

      unsigned short min = 130;
      unsigned short max = SCREEN_HALF_SIZE + 130;

      if (flip)
        radius = map(v, 0, 65535, max, min);
      else
        radius = map(v, 0, 65535, min, max);

      if (radius > SCREEN_HALF_SIZE)
        color = randomColor();

      minRadius = radius - aperture;
      maxRadius = radius + aperture;
    }

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      if (strip.idx == 0)
      {
        return CRGB::Black;
      }

      if (led.polar.radius >= minRadius && led.polar.radius <= maxRadius)
        return color;
      else
        return strip.buffer[led.idx];
    }

    void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, 30);
      }
    }
};


// Just a simple moodlight, but with three waves radiating to/from the center
class PolarMoodlight : public AbstractEffect
{
  public:
    // minBpm, maxBpm, minScale, maxScale
    RandSine<1, 15> red;
    RandSine<1, 15> green;
    RandSine<1, 15> blue;

    CRGB evaluate(LedStrip strip, Led led, milliseconds t) override
    {
      byte R = red.evaluate(led.polar.radius);
      byte G = green.evaluate(led.polar.radius);
      byte B = blue.evaluate(led.polar.radius);

      return CRGB(R, G, B);
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

  byte EFFECTS_COUNT = 10;

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
      retval = new PaletteWave();
      break;
    case 6:
      retval = new Lighthouse();
      break;
    case 7:
      retval = new PolarPaletteWave();
      break;
    case 8:
      retval = new PolarSwipe();
      break;
    case 9:
      retval = new PolarMoodlight();
      break;
    default:
      retval = new ErrorEffect();
      break;
  }

  retval->randomize();
  return retval;
}

#endif
