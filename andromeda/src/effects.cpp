#include "effects.h"

// Use each individual led strip as an independent moodlight.
// All leds in the same strip have the same color,
// but each strip fluctuates independently
class IndividualStripMoodlight : public AbstractEffect
{
  public:
    virtual const char* GetName()
    {
      return "IndividualStripMoodlight";
    }

    MoodLight moodlights[NUM_STRIPS];
    CRGB colors[NUM_STRIPS];

    void precompute(milliseconds t) override
    {
      FOR_EACH_STRIP
      {
        colors[iStrip] = moodlights[iStrip].evaluate();
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
      return colors[strip->idx];
    }
};

// Lights up the whole mirror blue, and randomly adds sparks of white
// that die out very quickly diffusing to neighbouring leds
class ElectricSparks : public AbstractEffect
{
  public:
    virtual const char* GetName()
    {
      return "ElectricSparks";
    }

    // Palette values for each LED
    // Needs double buffering to correctly compute the averaging of neighbouring pixels
    byte preValues[NUM_STRIPS][LEDS_PER_STRIP];
    byte newValues[NUM_STRIPS][LEDS_PER_STRIP];

    CRGBPalette256 palette;

    // Time scale factor for the perlin random noise
    RandParam<unsigned short, 200, 2000> hueTimeScale;

    // Current base hue (updated based on perlin noise)
    byte hue;

    // This is tricky to figure out if not by trial and error.
    // We roll a dice every frame for every led, so the chance must be really small.
    // These are values that I like experimentally, I cannot justify them.
    // TODO: better way to define the frequency
    constexpr static unsigned int DICE_LIMIT = 100000;
    EnergyParam<byte, 5, 26> sparkChance;

    // Chance that a spark becomes bigger, rolled out of 100.
    // If the roll is successful, the width is doubled and then rolled again until it fails.
    // Potentially going up to the full strip in rare cases.
    EnergyParam<byte, 40, 70> bigSparkChance;

    ElectricSparks()
    {
      // Professional software engineering
      memset8(preValues, 0, NUM_STRIPS * LEDS_PER_STRIP);
      memset8(newValues, 0, NUM_STRIPS * LEDS_PER_STRIP);
    }

    inline byte avg38(int a, int b, int c)
    {
      return (a + b + c) / 3;
    }

    void updatePalette()
    {
      // Create the palette based on the current hue.
      // - the first color at index 0 is the current hue, fully saturated;
      // - the last color is the complementary color of the base color, but partially desaturated
      // - the 1/3 color is the same as the base color, but half-saturated;
      // - the 2/3 color is pure white/
      // This logic was picked by trial and error and looks pretty nice.

      CHSV base = CHSV(hue, 255, 255);
      CHSV comp = CHSV(((short)hue + 128) % 256, 128, 255);
      CHSV desat = base;
      desat.s = 128;
      CHSV white = CHSV(0, 0, 255);

      CHSVPalette16 paletteTemp(base, desat, white, comp);

      // Upscale the palette so no interpolation is needed while running.
      // gives a completely imperceptible performace boost.
      UpscalePalette(paletteTemp, palette);
    }

    void randomize() override
    {
      hue = random(0, 256);
      updatePalette();
    }

    void precompute(milliseconds t) override
    {
      EVERY_N_MILLISECONDS(100)
      {
        hue = inoise8(t / hueTimeScale);
        updatePalette();
      }

      FOR_EACH_STRIP
      {
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
      FOR_EACH_STRIP
      {
        FOR_EACH_LED
        {
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
    virtual const char* GetName()
    {
      return "SaturationGlow";
    }

    // Time scale factor for the perlin random noise
    RandParam<unsigned short, 500, 5000> hueTimeScale;

    RandParam<byte, 100, 156> saturationCenter;         // skewed towards high saturation because colors are pretty
    byte saturationAmplitude;

    // Each strip has a random cycle time
    std::vector<RandParam<milliseconds, (1 MINUTES), (4 MINUTES)>> cycleTime;

    byte hue;                   // current hue (same for all strips)
    std::vector<CRGB> color;    // specific color per strip

    SaturationGlow() :
      cycleTime(NUM_STRIPS),  // this SHOULD call the default constructor of RandParam, picking 7 random values. I think.
      color(NUM_STRIPS)
    {
      saturationAmplitude = min(
        static_cast<byte>(saturationCenter),
        static_cast<byte>(255 - saturationCenter)
      );
    }

    void precompute(milliseconds t) override
    {
      hue = inoise8(t / hueTimeScale);

      FOR_EACH_STRIP
      {
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
    virtual const char* GetName()
    {
      return "PaletteWave";
    }

    CRGBPalette256 palette;
    RandParam<byte, 3, 8> bpm;
    RandParam<int, 5, 10> scale;

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

// Rotating beams of light
class NinjaStar : public AbstractEffect
{
  public:
    virtual const char* GetName()
    {
      return "NinjaStar";
    }

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
    virtual const char* GetName()
    {
      return "PolarSwipe";
    }

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
    virtual const char* GetName()
    {
      return "PolarMoodlight";
    }

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
    virtual const char* GetName()
    {
      return "RGBodyProblem";
    }

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


// Dreamed up by Claude!
// Optimized for Arduino with FastLED
class HexagonalRippleGalaxy : public AbstractEffect
{
private:
    // Cached values computed once per frame
    byte timeScale8;     // Time scaled to 0-255
    byte spiralOffset8;  // Spiral offset as 0-255
    byte baseHue;

    // Randomizable parameters for variation
    RandParam<byte, 3, 5> timeShift;          // Time scale divisor (>> 3 to >> 5) - faster
    RandParam<byte, 5, 7> spiralShift;        // Spiral rotation divisor (>> 5 to >> 7) - faster
    RandParam<byte, 6, 9> hueShift;           // Hue cycling divisor (>> 6 to >> 9) - faster

    RandParam<byte, 1, 3> ripple1Freq;        // Ripple 1 frequency multiplier
    RandParam<byte, 1, 4> ripple1TimeScale;   // Ripple 1 time multiplier
    RandParam<byte, 0, 2> ripple2Freq;        // Ripple 2 frequency multiplier (0 = off)
    RandParam<byte, 1, 3> ripple2TimeScale;   // Ripple 2 time multiplier

    RandParam<byte, 2, 5> spiralCount;        // Number of spiral arms
    RandParam<byte, 2, 4> spiralRadiusShift;  // Spiral-radius coupling (>> 2 to >> 4)

    RandParam<byte, 0, 2> hueAngleShift;      // Hue angle contribution (>> 0 to >> 2)
    RandParam<byte, 3, 5> hueRadiusShift;     // Hue radius contribution (>> 3 to >> 5)

public:
    const char* GetName() override
    {
        return "Hexagonal Ripple Galaxy";
    }

    void precompute(milliseconds t) override
    {
        // Scale time to 8-bit for FastLED trig functions
        // Use randomized time scaling
        timeScale8 = t >> static_cast<byte>(timeShift);

        // Spiral rotation with randomized speed
        spiralOffset8 = t >> static_cast<byte>(spiralShift);

        // Hue cycling with randomized period
        baseHue = t >> static_cast<byte>(hueShift);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds t) override
    {
        // Get position in millimeters
        float x = led->cartesian.x;
        float y = led->cartesian.y;

        // Calculate radius and angle using floating point for accuracy
        float radius = sqrt(x*x + y*y);
        float angle = atan2(y, x);

        // Convert to 8-bit values for FastLED functions
        byte radius8 = (byte)constrain(radius * 0.49f, 0, 255);  // Scale ~520mm max radius to 255
        byte angle8 = (byte)((angle + PI) * 40.584f);  // Map (-π,π) to (0,255)

        // Create ripples with randomized parameters
        byte ripple1 = sin8((radius8 * static_cast<byte>(ripple1Freq)) - (timeScale8 * static_cast<byte>(ripple1TimeScale)));
        byte ripple2 = sin8((radius8 * static_cast<byte>(ripple2Freq)) - (timeScale8 * static_cast<byte>(ripple2TimeScale)));

        // Combine ripples
        byte rippleSum = ((ripple1 >> 1) + (ripple2 >> 2)) + 64;

        // Add spiral component with randomized parameters
        byte spiral = sin8((angle8 * static_cast<byte>(spiralCount)) + spiralOffset8 + (radius8 >> static_cast<byte>(spiralRadiusShift)));

        // Combine ripples and spiral for saturation modulation instead of brightness
        byte saturationMod = ((rippleSum >> 1) + (spiral >> 1));

        // Create color with randomized hue distribution
        byte hue = baseHue + (angle8 >> static_cast<byte>(hueAngleShift)) + (radius8 >> static_cast<byte>(hueRadiusShift));

        // Use FastLED's built-in HSV to RGB conversion
        return CHSV(hue, 255, 255);  // Maximum saturation and brightness
    }
};


// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect()
{
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
      retval = new ElectricSparks();
      break;
    case 2:
      retval = new SaturationGlow();
      break;
    case 3:
      retval = new PaletteWave();
      break;
    case 4:
      retval = new NinjaStar();
      break;
    case 5:
      retval = new PolarSwipe();
      break;
    case 6:
      retval = new PolarMoodlight();
      break;
    case 7:
      retval = new RGBodyProblem();
      break;
    case 8:
      retval = new HexagonalRippleGalaxy();
      break;
    default:
      retval = new ErrorEffect();
      break;
  }

  retval->randomize();
  return retval;
}