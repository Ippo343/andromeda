#include "effects.h"

using std::vector;

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

    vector<MoodLight> moodlights;
    vector<CRGB> colors;

    IndividualStripMoodlight()
      : moodlights(GEOMETRY.getNumStrips()), colors(GEOMETRY.getNumStrips())
    {
    }

    void precompute(milliseconds_t t) override
    {
      FOR_EACH_STRIP
      {
        colors[iStrip] = moodlights[iStrip].evaluate();
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
      return colors[strip->idx];
    }
};


class ElectricSparks : public AbstractEffect
{
  public:
    virtual const char* GetName()
    {
      return "ElectricSparks";
    }

    // Palette values for each LED
    // Needs double buffering to correctly compute the averaging of neighbouring pixels
    vector<vector<uint8_t>> preValues;
    vector<vector<uint8_t>> newValues;

    CRGBPalette256 palette;

    // Time scale factor for the perlin random noise
    RandParam<unsigned short, 200, 2000> hueTimeScale;

    // Current base hue (updated based on perlin noise)
    uint8_t hue;

    // This is tricky to figure out if not by trial and error.
    // We roll a dice every frame for every led, so the chance must be really small.
    // These are values that I like experimentally, I cannot justify them.
    // TODO: better way to define the frequency
    constexpr static unsigned int DICE_LIMIT = 100000;
    EnergyParam<int, 5, 26> sparkChance;

    // Chance that a spark becomes bigger, rolled out of 100.
    // If the roll is successful, the width is doubled and then rolled again until it fails.
    // Potentially going up to the full strip in rare cases.
    EnergyParam<int, 40, 70> bigSparkChance;

    ElectricSparks()
    {
      // Allocate vectors for each strip
      preValues.resize(GEOMETRY.getNumStrips());
      newValues.resize(GEOMETRY.getNumStrips());

      for (size_t i = 0; i < GEOMETRY.getNumStrips(); i++) {
        preValues[i].resize(GEOMETRY.getStrip(i).num_leds, 0);
        newValues[i].resize(GEOMETRY.getStrip(i).num_leds, 0);
      }
    }

    inline int avg38(int a, int b, int c)
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

    void precompute(milliseconds_t t) override
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

        size_t stripLen = GEOMETRY.getStrip(iStrip).num_leds;
        for (size_t iLed = 0; iLed < stripLen; iLed++)
        {
          newValues[iStrip][iLed] = avg38(
            preValues[iStrip][LI(iLed - 1)],
            preValues[iStrip][iLed],
            preValues[iStrip][LI(iLed + 1)]
          );
        }
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
      // Random injection of new spikes
      if (random(DICE_LIMIT) < sparkChance)
      {
        int width = 1;

        // Keep rolling for a chance to increase the spark's size
        while (random(100) < bigSparkChance)
          width *= 2;

        // Now light up the pixel and its neighbours up to the defined width
        for (size_t w = 0; w < width; w++)
        {
          newValues[strip->idx][LI(led->idx + w)] = 255;
          newValues[strip->idx][LI(led->idx - w)] = 255;
        }
      }

      return ColorFromPalette(palette, preValues[strip->idx][led->idx]);
    }

    void postprocess(milliseconds_t t) override
    {
      // Dissipate the energy to lower values
      FOR_EACH_STRIP
      {
        size_t stripLen = GEOMETRY.getStrip(iStrip).num_leds;
        for (size_t iLed = 0; iLed < stripLen; iLed++)
        {
          newValues[iStrip][iLed] = scale8(newValues[iStrip][iLed], 254);
        }
      }

      // Copy the current buffer so that the next frame can diffuse it
      preValues = newValues;
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

    RandParam<uint8_t, 100, 156> saturationCenter;         // skewed towards high saturation because colors are pretty
    uint8_t saturationAmplitude;

    // Each strip has a random cycle time
    vector<RandParam<milliseconds_t, (1 MINUTES), (4 MINUTES)>> cycleTime;

    uint8_t hue;                   // current hue (same for all strips)
    vector<CRGB> color;    // specific color per strip

    SaturationGlow() :
      cycleTime(GEOMETRY.getNumStrips()),  // this SHOULD call the default constructor of RandParam, picking 7 random values. I think.
      color(GEOMETRY.getNumStrips())
    {
      saturationAmplitude = min(
        static_cast<uint8_t>(saturationCenter),
        static_cast<uint8_t>(255 - saturationCenter)
      );
    }

    void precompute(milliseconds_t t) override
    {
      hue = inoise8(t / hueTimeScale);

      FOR_EACH_STRIP
      {
        long scaledWave = scaledCubicWave8(t, cycleTime[iStrip], -saturationAmplitude, saturationAmplitude);
        uint8_t sat = constrain(saturationCenter + scaledWave, 0, 255);
        color[iStrip] = CHSV(hue, sat, 255);
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
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
    RandParam<uint8_t, 3, 8> bpm;
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

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
      int v = (led->cartesian.x + led->cartesian.y) / (int)scale;
      uint8_t value = beatsin8(bpm, 0, 255, 0, v);
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

    RandParam<milliseconds_t, 5000, 5000> duration;
    RandParam<unsigned short, 6, 6> beams;
    RandSign flip;

    MoodLight inner;
    MoodLight outer;
    CRGB innerColor;
    CRGB outerColor;
    uint8_t offset;

    void precompute(milliseconds_t t) override
    {
      innerColor = inner.evaluate();
      outerColor = outer.evaluate();
      offset = map((flip * t) % duration, 0, duration, 0, 255);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
      // TODO: great opportunity for an additional set of coordinates stored in the LED
      // This would allow effects to precompute scaled LED coordinates
      // Might have to be local to the effect in case of performance issues
      uint8_t theta = map((led->polar.cdegrees * beams) % FULL_CIRCLE, 0, FULL_CIRCLE, 0, 255);

      // TODO: this is a great opportunity for a LUT
      uint8_t v = sin8(theta + offset);
      for (size_t i = 0; i < 4; i++)
        v = scale8(v, v);

      unsigned short scaledRadius = map(led->polar.radius, 0, GEOMETRY.getScreenRadius(), 0, 255);
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
    RandParam<uint8_t, 10, 40> bpm;
    RandParam<uint8_t, 20, 80> bandWidth;

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
    unsigned short scanMax = GEOMETRY.getScreenRadius() + (bandWidth + 1);

    unsigned short bandCenter;
    CRGB color;

    void randomize() override
    {
      color = randomColor();
    }

    void precompute(milliseconds_t t) override
    {
      unsigned short v = beat16(bpm);

      if (flip)
        bandCenter = map(v, 0, 65535, scanMax, scanMin);
      else
        bandCenter = map(v, 0, 65535, scanMin, scanMax);

      if (bandCenter >= GEOMETRY.getScreenRadius() + bandWidth)
        color = randomColor();
    }

    inline uint8_t getBrightness(Led* led)
    {
      unsigned short R = led->polar.radius;
      unsigned short D = abs(R - bandCenter);

      if (D > bandWidth)
        return 0;
      else
        return map(D, 0, bandWidth, 255, 0);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
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

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
      uint8_t R = red.evaluate(led->polar.radius);
      uint8_t G = green.evaluate(led->polar.radius);
      uint8_t B = blue.evaluate(led->polar.radius);

      return CRGB(R, G, B);
    }
};

// Misnomer as it's not actually solving the 3 body problem... yet.
// Right now it only has 3 emitters moving independently via sine waves.
// The color of each LED is decided based on the distance from each emitter.
// It also has a single-channel mode where there is a single emitter
// hooked up to a moodlight source.
// TODO: use both 3 body problem and double pendulum
// TODO: render in the correct bounding box
class RGBodyProblem : public AbstractEffect
{
  public:
    virtual const char* GetName()
    {
      return "RGBodyProblem";
    }

    RandParam<int, 1, 3> emittersCount;
    RandBool fixedColors;

    vector<CartesianCoordinates> locations;
    vector<RandSine<1, 20>> sx;
    vector<RandSine<1, 20>> sy;
    vector<CHSV> colors;

    MoodLight moodlight;

    RGBodyProblem() :
      locations(emittersCount),
      colors(emittersCount),
      sx(emittersCount),
      sy(emittersCount)
    {
      controlHints = ControlHints::ROTATE_SPACE;

      for (size_t i; i < emittersCount; i++)
      {
        locations[i] = CartesianCoordinates();
        sx[i] = RandSine<1, 20>();
        sy[i] = RandSine<1, 20>();
      }

      // If there's only one emitter, it will use a moodlight instead
      colors = randomComplementaryColors(emittersCount);
    }

    // Helper to scale the result of a sine wave to the screen size
    short scale(int v)
    {
      return map(v, 0, 255, -GEOMETRY.getScreenRadius(), GEOMETRY.getScreenRadius());
    }

    void precompute(milliseconds_t t) override
    {
      for (size_t i = 0; i < emittersCount; i++)
      {
        locations[i].x = scale(sx[i].evaluate(0));
        locations[i].y = scale(sy[i].evaluate(0));
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
      if (emittersCount == 1)
      {
        CRGB rawColor = moodlight.evaluate();
        uint8_t v = brightnessFromEmitter(led, locations[0]);
        rawColor.nscale8(v);
        return rawColor;
      }

      CRGB finalColor = CRGB::Black;
      for (size_t i = 0; i < emittersCount; i++)
      {
        CRGB emitterColor = colors[i];
        uint8_t v = brightnessFromEmitter(led, locations[i]);
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
    uint8_t timeScale8;     // Time scaled to 0-255
    uint8_t spiralOffset8;  // Spiral offset as 0-255
    uint8_t baseHue;

    // Randomizable parameters for variation
    RandParam<uint8_t, 3, 5> timeShift;          // Time scale divisor (>> 3 to >> 5) - faster
    RandParam<uint8_t, 5, 7> spiralShift;        // Spiral rotation divisor (>> 5 to >> 7) - faster
    RandParam<uint8_t, 6, 9> hueShift;           // Hue cycling divisor (>> 6 to >> 9) - faster

    RandParam<uint8_t, 1, 3> ripple1Freq;        // Ripple 1 frequency multiplier
    RandParam<uint8_t, 1, 4> ripple1TimeScale;   // Ripple 1 time multiplier
    RandParam<uint8_t, 0, 2> ripple2Freq;        // Ripple 2 frequency multiplier (0 = off)
    RandParam<uint8_t, 1, 3> ripple2TimeScale;   // Ripple 2 time multiplier

    RandParam<uint8_t, 2, 5> spiralCount;        // Number of spiral arms
    RandParam<uint8_t, 2, 4> spiralRadiusShift;  // Spiral-radius coupling (>> 2 to >> 4)

    RandParam<uint8_t, 0, 2> hueAngleShift;      // Hue angle contribution (>> 0 to >> 2)
    RandParam<uint8_t, 3, 5> hueRadiusShift;     // Hue radius contribution (>> 3 to >> 5)

public:
    const char* GetName() override
    {
        return "Hexagonal Ripple Galaxy";
    }

    void precompute(milliseconds_t t) override
    {
        // Scale time to 8-bit for FastLED trig functions
        // Use randomized time scaling
        timeScale8 = t >> static_cast<uint8_t>(timeShift);

        // Spiral rotation with randomized speed
        spiralOffset8 = t >> static_cast<uint8_t>(spiralShift);

        // Hue cycling with randomized period
        baseHue = t >> static_cast<uint8_t>(hueShift);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
        // Get position in millimeters
        float x = led->cartesian.x;
        float y = led->cartesian.y;

        // Calculate radius and angle using floating point for accuracy
        float radius = sqrt(x*x + y*y);
        float angle = atan2(y, x);

        // Convert to 8-bit values for FastLED functions
        uint8_t radius8 = (uint8_t)constrain(radius * 0.49f, 0, 255);  // Scale ~520mm max radius to 255
        uint8_t angle8 = (uint8_t)((angle + PI) * 40.584f);  // Map (-π,π) to (0,255)

        // Create ripples with randomized parameters
        uint8_t ripple1 = sin8((radius8 * static_cast<uint8_t>(ripple1Freq)) - (timeScale8 * static_cast<uint8_t>(ripple1TimeScale)));
        uint8_t ripple2 = sin8((radius8 * static_cast<uint8_t>(ripple2Freq)) - (timeScale8 * static_cast<uint8_t>(ripple2TimeScale)));

        // Combine ripples
        uint8_t rippleSum = ((ripple1 >> 1) + (ripple2 >> 2)) + 64;

        // Add spiral component with randomized parameters
        uint8_t spiral = sin8((angle8 * static_cast<uint8_t>(spiralCount)) + spiralOffset8 + (radius8 >> static_cast<uint8_t>(spiralRadiusShift)));

        // Combine ripples and spiral for saturation modulation instead of brightness
        uint8_t saturationMod = ((rippleSum >> 1) + (spiral >> 1));

        // Create color with randomized hue distribution
        uint8_t hue = baseHue + (angle8 >> static_cast<uint8_t>(hueAngleShift)) + (radius8 >> static_cast<uint8_t>(hueRadiusShift));

        // Use FastLED's built-in HSV to RGB conversion
        return CHSV(hue, 255, 255);  // Maximum saturation and brightness
    }
};


// Each strip slowly drifts to a new random color independently
class IndividualStripDrift : public AbstractEffect
{
  public:
    virtual const char* GetName()
    {
      return "IndividualStripDrift";
    }

    // Random range for the transition duration
    EnergyParam<milliseconds_t,  2 SECONDS, 250> transitionDurationMin;
    EnergyParam<milliseconds_t, 20 SECONDS, 5 SECONDS> transitionDurationMax;

    // Per-strip timing info
    vector<milliseconds_t> transitionStartTimes;
    vector<milliseconds_t> transitionEndTimes;

    // Per-strip color info
    vector<CRGB> prevColors;
    vector<CRGB> targetColors;
    vector<CRGB> currentColors;

    IndividualStripDrift() :
      prevColors(GEOMETRY.getNumStrips(), CRGB::Black),
      targetColors(GEOMETRY.getNumStrips(), CRGB::Black),
      currentColors(GEOMETRY.getNumStrips(), CRGB::Black),
      transitionEndTimes(GEOMETRY.getNumStrips(), 0),
      transitionStartTimes(GEOMETRY.getNumStrips(), 0)
    {
    }

    void randomize() override
    {
      // Initialize all strips to a random color and set up the first transition
      milliseconds_t now = millis();
      FOR_EACH_STRIP
      {
        targetColors[iStrip] = randomColor();
        milliseconds_t duration = random(transitionDurationMin, transitionDurationMax);
        transitionStartTimes[iStrip] = now;
        transitionEndTimes[iStrip] = now + duration;
      }
    }

    void precompute(milliseconds_t t) override
    {
      FOR_EACH_STRIP
      {
        // Check if we need to pick a new color
        if (t >= transitionEndTimes[iStrip])
        {
          prevColors[iStrip] = targetColors[iStrip];
          targetColors[iStrip] = randomColor();
          transitionStartTimes[iStrip] = t;
          milliseconds_t duration = random(transitionDurationMin, transitionDurationMax);
          transitionEndTimes[iStrip] = t + duration;
        }

        // Compute the interpolation factor between the two colors
        uint8_t factor = cmap(t, transitionStartTimes[iStrip], transitionEndTimes[iStrip], 0, 255);
        factor = ease8InOutCubic(factor);

        currentColors[iStrip] = CRGB::blend(
          prevColors[iStrip],
          targetColors[iStrip],
          factor
        );
      }
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
      return currentColors[strip->idx];
    }
};


// Moodlight with three color waves propagating in random cartesian directions
// Optimized with integer math and memoization
class CartesianMoodlight : public AbstractEffect
{
  public:
    virtual const char* GetName()
    {
      return "CartesianMoodlight";
    }

    // Tunable parameters
    uint8_t valleyPower = 5;      // Power for stretching valleys (3-7 recommended)
    uint8_t maxWavelength = 5;    // Maximum crests per screen (1-10 range)

    // Random amplitude factors for color variation (scaled so max = 255)
    uint8_t redAmp, greenAmp, blueAmp;

    // Random temporal frequencies (BPM) for each color, stored as accum88
    RandParam<uint8_t, 1, 15> redBpm;
    RandParam<uint8_t, 1, 15> greenBpm;
    RandParam<uint8_t, 1, 15> blueBpm;

    // Precomputed direction vectors scaled by 256 for integer math
    short redDx, redDy;
    short greenDx, greenDy;
    short blueDx, blueDy;

    // Random wavelength scale factors stored as accum88 (8.8 fixed point)
    // Range: 0.5-2.0 crests per screen for long, sweeping waves
    accum88 redWavelength;
    accum88 greenWavelength;
    accum88 blueWavelength;

    // Memoization LUT for sin16 → power stretch
    // 256 entries, one for each possible 8-bit input
    uint8_t sinPowerLUT[256];

    CartesianMoodlight()
    {
      // Initialize LUT with sentinel value
      memset(sinPowerLUT, 0xFF, 256);
    }

    void randomize()
    {
      // Random BPM for temporal variation
      redBpm.randomize();
      greenBpm.randomize();
      blueBpm.randomize();

      // Generate random angles and precompute scaled direction vectors
      randomizeDirection(redDx, redDy);
      randomizeDirection(greenDx, greenDy);
      randomizeDirection(blueDx, blueDy);

      // Random wavelengths: 0.3 to maxWavelength crests per screen (wider variation)
      // Store as accum88: multiply float by 256
      redWavelength = random(30, maxWavelength * 100 + 1) * 256 / 100;
      greenWavelength = random(30, maxWavelength * 100 + 1) * 256 / 100;
      blueWavelength = random(30, maxWavelength * 100 + 1) * 256 / 100;

      // Generate random amplitude factors and scale so max = 255
      uint8_t r = random(256);
      uint8_t g = random(256);
      uint8_t b = random(256);

      uint8_t maxVal = max(r, max(g, b));
      uint8_t boost = 255 - maxVal;

      redAmp = r + boost;
      greenAmp = g + boost;
      blueAmp = b + boost;

      // Clear memoization cache
      memset(sinPowerLUT, 0xFF, 256);
    }

    CRGB evaluate(LedStrip* strip, Led* led, milliseconds_t t) override
    {
      // Compute dot products: direction · position
      // Results are in units of (mm * 256), divide by 256 for final distance
      int16_t redDist = ((long)redDx * led->cartesian.x + (long)redDy * led->cartesian.y) >> 8;
      int16_t greenDist = ((long)greenDx * led->cartesian.x + (long)greenDy * led->cartesian.y) >> 8;
      int16_t blueDist = ((long)blueDx * led->cartesian.x + (long)blueDy * led->cartesian.y) >> 8;

      // Calculate phase for each color using integer math
      // Spatial: distance * wavelength (both in fixed point)
      // Temporal: time * bpm * scaling factor
      uint16_t redPhase = computePhase(redDist, redWavelength, t, redBpm);
      uint16_t greenPhase = computePhase(greenDist, greenWavelength, t, greenBpm);
      uint16_t bluePhase = computePhase(blueDist, blueWavelength, t, blueBpm);

      // Evaluate with memoized power sine and apply amplitude factors
      uint8_t R = (evaluatePowerSine(redPhase) * redAmp) >> 8;
      uint8_t G = (evaluatePowerSine(greenPhase) * greenAmp) >> 8;
      uint8_t B = (evaluatePowerSine(bluePhase) * blueAmp) >> 8;

      return CRGB(R, G, B);
    }

  private:
    // Generate a random unit direction vector, scaled by 256 for integer math
    void randomizeDirection(short& dx, short& dy)
    {
      // Random angle in degrees (0-359)
      int angle = random(360);

      // Convert to radians and compute direction
      float rad = angle * PI / 180.0;
      dx = (short)(cos(rad) * 256.0);
      dy = (short)(sin(rad) * 256.0);
    }

    // Compute phase as uint16_t using integer math
    uint16_t computePhase(int16_t distance, accum88 wavelength, milliseconds_t t, uint8_t bpm)
    {
      // Spatial component:
      // distance: -260 to 260 mm
      // wavelength: 128 to 512 (0.5 to 2.0 in accum88)
      // We want: at wavelength=1.0 (256), distance=260 → phase ≈ 32768 (half cycle)
      // distance * wavelength = 260 * 256 = 66560
      // We want 32768, so multiply by 0.5: (distance * wavelength) >> 1
      int32_t spatial = ((int32_t)distance * wavelength) >> 1;

      // Temporal component: one full sine period (65536) per beat
      // At N BPM: 60000/N ms per beat
      // phase per ms = 65536 * bpm / 60000 ≈ bpm * 1.092
      // Using integer: (t * bpm * 70) >> 6
      int32_t temporal = ((int32_t)t * bpm * 70) >> 6;

      // Combine - both are now in the right scale for uint16_t
      uint16_t phase = (uint16_t)(spatial + temporal);

      return phase;
    }

    // Evaluate sin16 with power stretch and memoization
    uint8_t evaluatePowerSine(uint16_t phase)
    {
      // Get sin16 output (-32767 to 32767) and convert to uint8_t (0-255)
      int16_t sinVal = sin16(phase);
      uint8_t sinByte = (sinVal + 32768) >> 8;  // Map to 0-255

      // Check memoization cache
      if (sinPowerLUT[sinByte] != 0xFF) {
        return sinPowerLUT[sinByte];
      }

      // Compute power stretch: normalize to 0-1, raise to power, map to 0-255
      // Using integer math with 16-bit precision
      uint16_t normalized = (uint16_t)sinByte << 8;  // Scale to 0-65535
      uint32_t result = normalized;

      // Raise to power by repeated multiplication
      for (size_t i = 1; i < valleyPower; i++) {
        result = (result * normalized) >> 16;
      }

      uint8_t finalResult = result >> 8;  // Back to 0-255

      // Store in cache and return
      sinPowerLUT[sinByte] = finalResult;
      return finalResult;
    }
};


// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect()
{
  uint8_t EFFECTS_COUNT = 11;

  // Set this to the index of the effect you want to force while testing
  short forcedSelection = -1;

  static uint8_t previousSelection = 255;

  uint8_t selection;
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
    case 9:
      retval = new IndividualStripDrift();
      break;
    case 10:
      retval = new CartesianMoodlight();
      break;
    default:
      retval = new ErrorEffect();
      break;
  }

  retval->randomize();
  return retval;
}