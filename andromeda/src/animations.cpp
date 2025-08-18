#include "animations.h"

// ============================================================================
// SweepStrips - Internal Animation Class
// ============================================================================

// Sweeps all the strips with random colors,
// then white and then black sequentially
class SweepStrips : public AbstractAnimation
{
  public:
    virtual const char* GetName() { return "SweepStrips"; }

    RandParam<byte, 10, 30> timeStep;

    void run() override
    {
      std::vector<CHSV> colors = randomComplementaryColors(3);

      paint(CRGB::Black);
      for (byte c = 0; c < 3; c++)
        colorSweep(colors[c]);
      colorSweep(CRGB::White);
      colorSweep(CRGB::Black);
    }

  private:
    void colorSweep(CRGB color)
    {
      FOR_EACH_LED
      {
        FOR_EACH_STRIP
        {
          STRIPS[iStrip].buffer[iLed] = color;
        }
        FastLED.show();
        delay(timeStep);
      }
    }
};


// ============================================================================
// BaseSweep - Abstract Base Class for Sweep Animations
// ============================================================================

class BaseSweep : public AbstractAnimation
{
  protected:
    bool direction; // Subclasses define meaning (clockwise/outward, etc.)
    unsigned short sweepDuration;
    unsigned short rampWidth;

    // Pure virtual functions that subclasses must implement
    virtual void flipCoordinates() = 0;
    virtual unsigned short getCoordinate(int strip, int led) = 0;
    virtual unsigned short getMaxCoordinate() = 0;

    void run() override
    {
      std::vector<CHSV> colors = randomComplementaryColors(3);

      // Coordinate system flip for direction (if needed)
      if (!direction)
      {
        flipCoordinates();
      }

      paint(CRGB::Black);
      for (byte c = 0; c < 3; c++)
      {
        colorSweep(colors[c]);
      }
      colorSweep(CRGB::White);
      colorSweep(CRGB::Black);
    }

  private:
    void colorSweep(CRGB color)
    {
      short coordLead = 0;  // The coordinate of the leading edge of the ramp
      short coordTail = 0;  // The coordinate of the trailing edge of the ramp
      unsigned short maxCoord = getMaxCoordinate();

      milliseconds_t start = millis();
      milliseconds_t t = 0;

      while (t <= sweepDuration)
      {
        // Always sweep in positive direction from 0 to max + rampWidth
        coordLead = map(t, 0, sweepDuration, 0, maxCoord + rampWidth);
        coordTail = coordLead - rampWidth;

        FOR_EACH_STRIP
        {
          FOR_EACH_LED
          {
            unsigned short coord = getCoordinate(iStrip, iLed);

            // Range check with boundary handling
            bool inRange;
            if (coordLead > maxCoord)
            {
              // Handle overflow at the boundary
              if (maxCoord == FULL_CIRCLE) // Angular case - wraparound
              {
                inRange = (coord >= coordTail) || (coord <= (coordLead - maxCoord));
              }
              else // Radial case - no wraparound
              {
                inRange = (coord >= coordTail);
              }
            }
            else if (coordTail < 0)
            {
              // Handle negative tail at the beginning of sweep
              if (maxCoord == FULL_CIRCLE) // Angular case - wraparound
              {
                inRange = (coord >= (coordTail + maxCoord)) || (coord <= coordLead);
              }
              else // Radial case - no wraparound
              {
                inRange = (coord <= coordLead);
              }
            }
            else
            {
              // Normal case - no boundary issues
              inRange = (coord >= coordTail && coord <= coordLead);
            }

            if (inRange)
            {
              // Calculate brightness based on distance from leading edge
              short rampDistance;
              if (maxCoord == FULL_CIRCLE && coordLead > maxCoord && coord <= (coordLead - maxCoord))
              {
                // LED is in the wrapped portion (angular case)
                rampDistance = (coordLead - maxCoord) - coord;
              }
              else if (maxCoord == FULL_CIRCLE && coordTail < 0 && coord >= (coordTail + maxCoord))
              {
                // LED is in the wrapped portion (negative tail case, angular)
                rampDistance = coordLead - (coord - maxCoord);
              }
              else
              {
                // Normal case (both angular and radial)
                rampDistance = coordLead - coord;
              }

              byte brightness = map(rampDistance, 0, rampWidth, 0, 255);
              STRIPS[iStrip].buffer[iLed] = color % brightness;
            }
          }
        }

        FastLED.show();
        t = millis() - start;
      }
    }
};

// ============================================================================
// ClockSweep - Angular Sweep Animation
// ============================================================================

class ClockSweep : public BaseSweep
{
  public:
    virtual const char* GetName() { return "ClockSweep"; }

    RandBool clockwise;
    RandParam<unsigned short, 400, 800> duration;

    ClockSweep()
    {
      direction = clockwise;
      sweepDuration = duration;
      rampWidth = 1000; // 10 degrees
    }

  protected:
    void flipCoordinates() override
    {
      FOR_EACH_STRIP
      {
        FOR_EACH_LED
        {
          STRIPS[iStrip].leds[iLed].polar.cdegrees = FULL_CIRCLE - STRIPS[iStrip].leds[iLed].polar.cdegrees;
        }
      }
      }

    unsigned short getCoordinate(int strip, int led) override
    {
      return STRIPS[strip].leds[led].polar.cdegrees;
    }

    unsigned short getMaxCoordinate() override
    {
      return FULL_CIRCLE;
    }
};

// ============================================================================
// RadialSweep - Radial Sweep Animation
// ============================================================================

class RadialSweep : public BaseSweep
{
  public:
    virtual const char* GetName() { return "RadialSweep"; }

    RandBool outward; // For external API compatibility
    RandParam<milliseconds_t, 300, 750> duration;

    RadialSweep()
    {
      direction = outward;
      sweepDuration = duration;
      rampWidth = 10; // Radial ramp width in distance units
    }

  protected:
    void flipCoordinates() override
    {
      FOR_EACH_STRIP
      {
        FOR_EACH_LED
        {
          STRIPS[iStrip].leds[iLed].polar.radius = SCREEN_HALF_SIZE - STRIPS[iStrip].leds[iLed].polar.radius;
        }
      }
    }

    unsigned short getCoordinate(int strip, int led) override
    {
      return STRIPS[strip].leds[led].polar.radius;
    }

    unsigned short getMaxCoordinate() override
    {
      return SCREEN_HALF_SIZE;
    }
};


// ============================================================================
// SequentialFadeIn - Internal Animation Class
// ============================================================================

// Fade in each strip with a random color
class SequentialFadeIn : public AbstractAnimation
{
  public:
    virtual const char* GetName() { return "SequentialFadeIn"; }

    RandParam<milliseconds_t, 150, 500> fadeIn;
    milliseconds_t fadeOut = 2 * fadeIn;

    void run() override
    {
      paint(CRGB::Black);

      // Shuffle the strip indices to randomize the order of fading in
      int strips[NUM_STRIPS];
      for (int i = 0; i < NUM_STRIPS; i++) strips[i] = i;
      shuffle(strips, NUM_STRIPS);

      auto colors = randomComplementaryColors(NUM_STRIPS);

      for (byte i = 0; i < NUM_STRIPS; i++)
      {
        fadeInStrip(strips[i], colors[i], fadeIn);
      }

      milliseconds_t start = millis();
      milliseconds_t dt;
      do
      {
        dt = millis() - start;
        byte b = constrain(map(dt, 0, fadeOut, 255, 0), 0, 255);
        FastLED.setBrightness(b);
        FastLED.show();
      }
      while (dt < fadeOut);
    }
};


// ============================================================================
// Swipe - Internal Animation Class
// ============================================================================

// Swipes a random color from left to right and then fades it out
class Swipe : public AbstractAnimation
{
  public:
    virtual const char* GetName() { return "Swipe"; }

    Swipe()
    {
      controlHints |= ControlHints::ROTATE_SPACE;
    }

    void run() override
    {
      paint(CRGB::Black);
      CRGB color = randomColor();

      // Even without any delay, scrolling the whole screen size takes a long time
      // (which I am a bit suspicious of to be honest, but I guess calling it 520 times is a bit much).
      // It looks smoother if you increase in steps of 2 or 3
      RandParam<short, 2, 3> step;

      // It goes to (step * SCREEN_HALF_SIZE) so that the fading trail has time to fully fade out
      for (short v = -SCREEN_HALF_SIZE; v <= (step * SCREEN_HALF_SIZE); v += step)
      {
        FOR_EACH_STRIP
        {
          FOR_EACH_LED
          {
            short lv = STRIPS[iStrip].leds[iLed].cartesian.x;
            if (lv >= (v - step) && lv <= v)
              STRIPS[iStrip].buffer[iLed] = color;
          }

          fadeToBlackBy(STRIPS[iStrip].buffer, LEDS_PER_STRIP, step);
        }

        FastLED.show();
      }
    }
};


// ============================================================================
// WiFiConnectingAnimation - External Animation Class Implementation
// ============================================================================

const char* WiFiConnectingAnimation::GetName()
{
  return "WiFiConnectingAnimation";
}

void WiFiConnectingAnimation::run()
{
  paint(CRGB::Black);
  paintStrip(0, CRGB::SteelBlue);
  FastLED.show();
}


// ============================================================================
// WiFiSuccessAnimation - External Animation Class Implementation
// ============================================================================

const char* WiFiSuccessAnimation::GetName()
{
  return "WiFiSuccessAnimation";
}

void WiFiSuccessAnimation::run()
{
  paint(CRGB::Black);
  paintStrip(0, CRGB::Green);
  FastLED.show();
  delay(250);
}


// ============================================================================
// ErrorAnimation - External Animation Class Implementation
// ============================================================================

const char* ErrorAnimation::GetName()
{
  return "ErrorAnimation";
}

void ErrorAnimation::run()
{
  milliseconds_t flashDuration = 250;
  paint(CRGB::Black);

  for (byte i = 0; i < 3; i++)
  {
    paintStrip(0, CRGB::Red);
    FastLED.show();
    delay(flashDuration);

    paintStrip(0, CRGB::Black);
    FastLED.show();
    delay(flashDuration);
  }
}


// ============================================================================
// Utility Functions
// ============================================================================

AbstractAnimation* getRandomAnimation()
{
  byte ANIMATIONS_COUNT = 5;

  // Set this to the index of the animation you want to force while testing
  short forcedSelection = -1;

  static byte previousSelection = 255;

  byte selection;
  if (forcedSelection >= 0)
    selection = forcedSelection;
  else
    do
      selection = random(ANIMATIONS_COUNT);
    while (selection == previousSelection);

  previousSelection = selection;

  switch (selection)
  {
    case 0:
      return new SweepStrips();
    case 1:
      return new SequentialFadeIn();
    case 2:
      return new ClockSweep();
    case 3:
      return new RadialSweep();
    case 4:
      return new Swipe();
    default:
      return new ErrorAnimation();
  }
}