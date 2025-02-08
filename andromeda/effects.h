#ifndef EFFECTS_H
#define EFFECTS_H

#include <vector>
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
    virtual const char* GetName() { return "IndividualStripMoodlight"; }

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
    virtual const char* GetName() { return "LoopingPoint"; }

    MoodLight moodlights[NUM_STRIPS];
    RandParam<milliseconds, 30, 60> step;
    byte idxOn;
    byte v;

    CRGB color[NUM_STRIPS];

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {
        color[iStrip] = moodlights[iStrip].evaluate();
      }

      // Pick an LED to turn on, rotating along the strip
      idxOn = (int)(t / step) % (LEDS_PER_STRIP);

      v = cmap(t % step, 0, step, 0, 255);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      // If this is the led to turn on, apply the color;
      // otherwise, leave it unchanged.
      // They will be faded away in postprocessing
      return led->idx == idxOn
        ? color[strip->idx] % v
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
    virtual const char* GetName() { return "ElectricSparks"; }

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
    RandParam<byte, 5, 20> sparkChance;

    // Chance that a spark becomes bigger, rolled out of 100.
    // If the roll is successful, the width is doubled and then rolled again until it fails.
    // Potentially going up to the full strip in rare cases.
    RandParam<byte, 40, 70> bigSparkChance;

    ElectricSparks()
    {
      // Professional software engineering
      memset8(preValues, 0, NUM_STRIPS * LEDS_PER_STRIP);
      memset8(newValues, 0, NUM_STRIPS * LEDS_PER_STRIP);
    }

    byte avg38(byte a, byte b, byte c)
    {
      int newV = a + b + c;
      return newV / 3;
    }

    void randomize() override
    {
      // Create a new random palette:
      // - the first color at index 0 is random and fully saturated;
      // - the last color is the complementary color of the base color, but partially desaturated
      // - the 1/3 color is the same as the base color, but half-saturated;
      // - the 2/3 color is pure white/
      // This logic was picked by trial and error and looks pretty nice.

      std::vector<CHSV> colors = randomComplementaryColors(2);
      CHSV desat = colors[0]; desat.s = 128;
      CHSV white = CHSV(0, 0, 255);
      colors[1].s = 128;

      CHSVPalette16 paletteTemp(colors[0], desat, white, colors[1]);

      // Upscale the palette so no interpolation is needed while running.
      // gives a completely imperceptible performace boost.
      UpscalePalette(paletteTemp, palette);
    }

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP {

        // Value diffusion between neighbouring LEDs:
        // each LED becomes the average of its neighbours (poor man's heat conduction),
        // taking the value from the previous buffer so that the new buffer is computed correctly
        // TODO: solve the heat conduction partial differential equation (LOL)

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
    virtual const char* GetName() { return "SaturationGlow"; }

    static const byte hueAmplitude = 10;
    RandParam<byte, hueAmplitude, 255 - hueAmplitude> hueCenter;
    RandParam<accum88, 1, 10> hueBpm88;
    byte hue;

    RandParam<byte, 100, 156> saturationCenter;         // skewed towards high saturation because colors are pretty
    byte saturationAmplitude;

    std::vector<RandParam<milliseconds, (1 MINUTES), (4 MINUTES)>> cycleTime;
    std::vector<CRGB> color;

    SaturationGlow() :
      cycleTime(NUM_STRIPS),  // this SHOULD call the default constructor of RandParam, picking 7 random values. I think.
      color(NUM_STRIPS)
    {
      saturationAmplitude = min(saturationCenter, (255 - saturationCenter));
    }

    void precompute(milliseconds t) override
    {
      hue = beatsin88(hueBpm88, hueCenter - hueAmplitude, hueCenter + hueAmplitude, 255);

      // TODO: maybe this should use a regular sine wave?
      FOR_EACH_STRIP {
        long scaledWave = scaledCubicWave8(t, cycleTime[iStrip], -saturationAmplitude, saturationAmplitude);
        byte sat = constrain(saturationCenter + scaledWave, 0, 255);
        color[iStrip] = CHSV(hue, sat, 255);
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      return color[strip->idx];
    }
};


class PaletteWave : public AbstractEffect
{
  public:
    virtual const char* GetName() { return "PaletteWave"; }

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


// Randomly light up a whole strip with a random color,
// and then keep everything fading to black.
// Looks a little bit like fireworks.
class Fireworks: public AbstractEffect
{
  public:
    virtual const char* GetName() { return "Fireworks"; }

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
class NinjaStar : public AbstractEffect
{
  public:
    virtual const char* GetName() { return "NinjaStar"; }

    RandParam<milliseconds, 5000, 5000> duration;
    RandParam<unsigned short, 6, 6> beams;
    RandSign flip;

    MoodLight inner;
    MoodLight outer;
    CRGB innerColor;
    CRGB outerColor;
    byte offset;

    void precompute(milliseconds t) override
    {
      innerColor = inner.evaluate();
      outerColor = outer.evaluate();
      offset = map((flip * t) % duration, 0, duration, 0, 255);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      // TODO: great opportunity for an additional set of coordinates stored in the LED
      // This would allow effects to precompute scaled LED coordinates
      // Might have to be local to the effect in case of performance issues
      byte theta = map((led->polar.cdegrees * beams) % FULL_CIRCLE, 0, FULL_CIRCLE, 0, 255);

      // TODO: this is a great opportunity for a LUT
      byte v = sin8(theta + offset);
      for (byte i = 0; i < 4; i++)
        v = scale8(v, v);

      unsigned short scaledRadius = map(led->polar.radius, 0, SCREEN_HALF_SIZE, 0, 255);
      CRGB color = blend(innerColor, outerColor, scaledRadius);

      return color % v;
    }
};


// Keeps swiping and fading a color through the radius
class PolarSwipe : public AbstractEffect
{
  public:
    virtual const char* GetName() { return "PolarSwipe"; }

    RandBool flip;
    RandParam<byte, 10, 40> bpm;
    RandParam<byte, 20, 80> bandWidth;

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
    unsigned short scanMin = bandWidth / 2;
    unsigned short scanMax = SCREEN_HALF_SIZE + (bandWidth + 1);

    unsigned short bandCenter;
    CRGB color;

    void randomize() override
    {
      color = randomColor();
    }

    void precompute(milliseconds t) override
    {
      unsigned short v = beat16(bpm);

      if (flip)
        bandCenter = map(v, 0, 65535, scanMax, scanMin);
      else
        bandCenter = map(v, 0, 65535, scanMin, scanMax);

      if (bandCenter >= SCREEN_HALF_SIZE + bandWidth)
        color = randomColor();
    }

    inline byte getBrightness(Led* led)
    {
      unsigned short R = led->polar.radius;
      unsigned short D = abs(R - bandCenter);

      if (D > bandWidth)
        return 0;
      else
        return map(D, 0, bandWidth, 255, 0);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      // The central strip is excluded because honestly it just looks weird,
      // it adds a sort of sudden "pop" that looks ugly
      if (strip->idx == 0)
        return CRGB::Black;

      return color % getBrightness(led);
    }
};


// Just a simple moodlight, but with three waves radiating to/from the center
class PolarMoodlight : public AbstractEffect
{
  public:
    virtual const char* GetName() { return "PolarMoodlight"; }

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
    virtual const char* GetName() { return "RGBodyProblem"; }

    RandParam<byte, 1, 3> emittersCount;
    RandBool fixedColors;

    std::vector<CartesianCoordinates> locations;
    std::vector<RandSine<1, 20>> sx;
    std::vector<RandSine<1, 20>> sy;
    std::vector<CHSV> colors;

    MoodLight moodlight;

    RGBodyProblem() :
      locations(emittersCount),
      colors(emittersCount),
      sx(emittersCount),
      sy(emittersCount)
    {
      controlHints = ControlHints::ROTATE_SPACE;

      for (byte i; i < emittersCount; i++)
      {
        locations[i] = CartesianCoordinates();
        sx[i] = RandSine<1, 20>();
        sy[i] = RandSine<1, 20>();
      }

      // If there's only one emitter, it will use a moodlight instead
      colors = randomComplementaryColors(emittersCount);
    }

    // Helper to scale the result of a sine wave to the screen size
    short scale(byte v)
    {
      return map(v, 0, 255, -SCREEN_HALF_SIZE, SCREEN_HALF_SIZE);
    }

    void precompute(milliseconds t) override
    {
      for (byte i = 0; i < emittersCount; i++)
      {
        locations[i].x = scale(sx[i].evaluate(0));
        locations[i].y = scale(sy[i].evaluate(0));
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      if (emittersCount == 1)
      {
        CRGB rawColor = moodlight.evaluate();
        byte v = brightnessFromEmitter(led, locations[0]);
        rawColor.nscale8(v);
        return rawColor;
      }

      CRGB finalColor = CRGB::Black;
      for (byte i = 0; i < emittersCount; i++)
      {
        CRGB emitterColor = colors[i];
        byte v = brightnessFromEmitter(led, locations[i]);
        emitterColor.nscale8(v);
        finalColor += emitterColor;
      }

      return finalColor;
    }
};


// I don't think this will ever show, but why not
class ErrorEffect : public AbstractEffect
{
  public:
    virtual const char* GetName() { return "ErrorEffect"; }

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
      retval = new SaturationGlow();
      break;
    case 4:
      retval = new Fireworks();
      break;
    case 5:
      retval = new PaletteWave();
      break;
    case 6:
      retval = new NinjaStar();
      break;
    case 7:
      retval = new PolarSwipe();
      break;
    case 8:
      retval = new PolarMoodlight();
      break;
    case 9:
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
