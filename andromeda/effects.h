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

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      return colors[strip->idx];
    }
};


// One single led turned on per strip,
// looping around it very fast
class LoopingPoint : public AbstractEffect
{
  public:
    MoodLight moodlights[NUM_STRIPS];
    RandParam<milliseconds, 30, 60> step;
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

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      // If this is the led to turn on, apply the color;
      // otherwise, leave it unchanged.
      // They will be faded away in postprocessing
      return led->idx == idxOn
        ? color[strip->idx]
        : strip->buffer[led->idx];
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
    unsigned int DICE_LIMIT = 100000;
    RandParam<byte, 5, 40> sparkChance;

    // Chance that a spark becomes bigger, rolled out of 100.
    // If the roll is successful, the width is doubled and then rolled again until it fails.
    // Potentially going up to the full strip in rare cases.
    RandParam<byte, 10, 40> bigSparkChance;

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

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
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
          newValues[strip->idx][LI(led->idx + w)] = 255;
          newValues[strip->idx][LI(led->idx - w)] = 255;
        }
      }

      return ColorFromPalette(palette, preValues[strip->idx][led->idx]);
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


// Whole mirror moodlight pulsating around a central saturation
class SaturationGlow : public AbstractEffect
{
  public:
    RandParam<milliseconds, (1 MINUTES), (4 MINUTES)> cycleTime;
    RandParam<byte, 0, 255> hue;
    RandParam<byte, 128, 220> saturationCenter;         // skewed towards high saturation because colors are pretty
    byte saturationAmplitude = 255 - saturationCenter;
    CRGB color;

    void precompute(milliseconds t) override
    {
      long scaledWave = scaledCubicWave8(t, cycleTime, -saturationAmplitude, saturationAmplitude);
      byte sat = constrain(saturationCenter + scaledWave, 0, 255);
      color = CHSV(hue, sat, 255);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      return color;
    }
};


class PaletteWave : public AbstractEffect
{
  public:
    CRGBPalette256 palette;
    RandParam<byte, 2, 10> bpm;
    RandParam<byte, 1, 5> scale;

    PaletteWave()
    {
      controlHints |= ControlHints::ROTATE_SPACE;
    }

    void randomize() override
    {
      CRGBPalette16 palette16 = randomPredefinedPalette();
      UpscalePalette(palette16, palette);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      int v = (led->cartesian.x + led->cartesian.y) / (int)scale;
      byte value = beatsin8(bpm, 0, 255, 0, v);
      return ColorFromPalette(palette, value);
    }
};


// Just like the PaletteWave effect, but in polar coordinates
class PolarPaletteWave : public AbstractEffect
{
  public:
    CRGBPalette256 palette;
    RandParam<byte, 1, 10> bpm;
    RandParam<unsigned short, 1, 5> scale;
    RandSign flip;

    void randomize() override
    {
      CRGBPalette16 palette16 = randomPredefinedPalette();
      UpscalePalette(palette16, palette);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      short v = flip * led->polar.radius / scale;
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
    RandParam<byte, 30, 70> sparkChance;
    RandParam<unsigned short, 1, 10> bigSparkChance;

    void precompute(milliseconds t) override
    {
      bool bigSpark = (random(DICE_LIMIT) < bigSparkChance);

      FOR_EACH_STRIP {
        // Randomly fill the whole strip with a random color
        if (bigSpark || random(DICE_LIMIT) < sparkChance)
          paintStrip(iStrip, randomColor());
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      return STRIPS[strip->idx].buffer[led->idx];
    }

    void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, 5);
      }
    }
};


// Rotating beams of light
class Lighthouse : public AbstractEffect
{
  public:

    RandParam<unsigned short, 6, 12> bpm;
    RandParam<unsigned short, 1, 3> beams;
    RandParam<unsigned short, 1500, 4500> baseAperture;
    RandBool flip;

    unsigned short aperture = baseAperture / beams;

    unsigned short angle;
    unsigned short minAngle;
    unsigned short maxAngle;
    MoodLight moodlight;
    CRGB color;

    unsigned short range = (FULL_CIRCLE / beams);

    void precompute(milliseconds t) override
    {
      color = moodlight.evaluate();

      unsigned short v = beat16(bpm * beams);

      if (flip)
        angle = map(v, 0, 65535, 0, range);
      else
        angle = map(v, 0, 65535, range, 0);

      minAngle = (angle - aperture) % range;
      maxAngle = (angle + aperture) % range;
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      bool on;
      unsigned short deg = led->polar.cdegrees % range;

      // Correctly handle the final part of the rotation,
      // where maxAngle has already rolled over the zero line but minAngle hasn't
      if (minAngle > maxAngle)
        on = (deg >= minAngle || deg <= maxAngle);
      else
        on = (deg >= minAngle && deg <= maxAngle);

      if (on)
        return color;
      else
        return strip->buffer[led->idx];
    }

    void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, 2 * bpm * beams);
      }
    }
};


// Keeps swiping and fading a color through the radius
class PolarSwipe : public AbstractEffect
{
  public:

    RandBool flip;
    RandParam<byte, 10, 40> bpm;
    RandParam<byte, 20, 80> aperture;

    // Minimum and maximum radii for the swipe.
    // It needs to start from aperture / 2 so that the minimum of the band is at 0,
    // and it needs to finish at the edge of the screen + aperture for the same reason.
    //
    // And then finally you need a 1mm buffer: we need to push the band completely outside
    // of the screen, because when that is out the color is chosen randomly.
    // But without this buffer, the LEDs at the very edge of the structure are technically
    // just inside the band, and so they get a new random color for a few consecutive frames
    // causing an annoying color flicker at the edge.
    //
    unsigned short scanMin = aperture / 2;
    unsigned short scanMax = SCREEN_HALF_SIZE + (aperture + 1);

    unsigned short radius;
    unsigned short bandMin;
    unsigned short bandMax;
    CRGB color;

    void randomize() override
    {
      color = randomColor();
    }

    void precompute(milliseconds t) override
    {
      unsigned short v = beat16(bpm);

      if (flip)
        radius = map(v, 0, 65535, scanMax, scanMin);
      else
        radius = map(v, 0, 65535, scanMin, scanMax);

      if (radius >= SCREEN_HALF_SIZE + aperture)
        color = randomColor();

      bandMin = radius - aperture;
      bandMax = radius + aperture;
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      // The central strip is excluded because honestly it just looks weird,
      // it adds a sort of sudden "pop" that looks ugly
      if (strip->idx == 0)
        return CRGB::Black;

      if (led->polar.radius >= bandMin && led->polar.radius <= bandMax)
        return color;
      else
        return strip->buffer[led->idx];
    }

    void postprocess(milliseconds t) override
    {
      FOR_EACH_STRIP {
        fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, bpm);
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

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      byte R = red.evaluate(led->polar.radius);
      byte G = green.evaluate(led->polar.radius);
      byte B = blue.evaluate(led->polar.radius);

      return CRGB(R, G, B);
    }
};


// Misnomer as it's not actually solving the 3 body problem... yet.
// Right now it only has 3 emitters moving independently via sine waves.
// The color of each LED is decided based on the distance from each emitter.
// It also has a single-channel mode where there is a single emitter
// hooked up to a moodlight source.
class RGBodyProblem : public AbstractEffect
{
  public:

    // If this is false, use only the Red emitter
    RandBool rgbMode;

    // Independent sine generators for each emitter's (x,y) coordinates
    // TODO: make this even more chaotic
    RandSine<1, 20> sxR;
    RandSine<1, 20> syR;
    RandSine<1, 20> sxG;
    RandSine<1, 20> syG;
    RandSine<1, 20> sxB;
    RandSine<1, 20> syB;

    // Location of the 3 emitters
    CartesianCoordinates R;
    CartesianCoordinates G;
    CartesianCoordinates B;

    MoodLight moodlight;

    RGBodyProblem()
    {
      controlHints = ControlHints::ROTATE_SPACE;
    }

    // This factor control the final brightness of each channel.
    // 255 is obviously the maximum brightness: but then you need to multiply but some factor
    // because otherwise (255 / d^2) is always very very dim.
    // I found 5000 by trial and error and it looks good.
    const float brightnessFactor = 255 * 5000;  // * (1 / distance^2)

    // Helper to scale the result of a sine wave to the screen size
    short scale(byte v)
    {
      return map(v, 0, 255, -SCREEN_HALF_SIZE, SCREEN_HALF_SIZE);
    }

    void precompute(milliseconds t) override
    {
      R.x = scale(sxR.evaluate(0));
      R.y = scale(syR.evaluate(0));

      // In single channel mode only the R emitter is used,
      // no need to update the other 2
      if (rgbMode)
      {
        G.x = scale(sxG.evaluate(0));
        G.y = scale(syG.evaluate(0));

        B.x = scale(sxB.evaluate(0));
        B.y = scale(syB.evaluate(0));
      }
    }

    byte component(Led* led, CartesianCoordinates e)
    {
      short dx = ( led->cartesian.x - e.x );
      short dy = ( led->cartesian.y - e.y );

      // This is the famouse fast inverse square root and boy is it fast.
      // With one single channel, doing everything in floating point with the standard library
      // was tanking the framerate below 40. With the fast algorithm, it runs 3 channels at 75 fps!
      float invdist = Q_rsqrt(dx*dx + dy*dy);
      byte v = (byte)constrain(brightnessFactor * invdist * invdist, 0, 255);

      // Original formula with just the inverse of the distance.
      // Physically correct, but it looks kinda dull.
      // (1/d^2) looks cooler.
      // byte v = (byte)constrain(255 * 30 * invdist , 0, 255);

      return v;
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      if (rgbMode)
        return CRGB(component(led, R), component(led, G), component(led, B));
      else
      {
        CRGB rawColor = moodlight.evaluate();
        byte v = component(led, R);
        return CRGB(scale8(rawColor.r, v), scale8(rawColor.g, v), scale8(rawColor.b, v));
      }
    }
};


// I don't think this will ever show, but why not
class ErrorEffect : public AbstractEffect
{
  public:
    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      if (strip->idx == 0)
        return CRGB::Red;
      else
        return CRGB::Black;
    }
};


// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect() {

  byte EFFECTS_COUNT = 11;

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
      retval = new SaturationGlow();
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
    case 10:
      retval = new RGBodyProblem();
      break;
    default:
      retval = new ErrorEffect();
      break;
  }

  retval->randomize();
  return retval;
}

#endif
