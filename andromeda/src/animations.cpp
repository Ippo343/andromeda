#include "animations.h"
#include "geometry.h"
#include "effects-utils.h"

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
// ClockSweep - Internal Animation Class
// ============================================================================

class ClockSweep : public AbstractAnimation
{
  public:
    virtual const char* GetName() { return "ClockSweep"; }

    RandBool clockwise;
    RandParam<unsigned short, 400, 800> sweepDuration;
    const unsigned short rampWidth = 1000;  // 10 degrees

    void run() override
    {
      std::vector<CHSV> colors = randomComplementaryColors(3);

      // Coordinates trick: implementing both the clockwise and counterclockwise
      // rotation results in A LOT of code and corner cases. Claude was writing that for me,
      // but honestly it was fugly. And then I realized that I can just flip the coordinate system
      // and only keep the clockwise implementation.
      // The MissionControl logic resets the coordinates before each effect,
      // so we don't even need to cleanup.
      if (!clockwise)
      {
        FOR_EACH_STRIP
        {
          FOR_EACH_LED
          {
            STRIPS[iStrip].leds[iLed].polar.cdegrees = FULL_CIRCLE - STRIPS[iStrip].leds[iLed].polar.cdegrees;
          }
        }
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
      // Simple clockwise implementation - coordinate remapping handles direction
      short thetaLead = 0;  // The angle of the leading edge of the ramp
      short thetaTail = 0;  // The angle of the trailing edge of the ramp

      milliseconds start = millis();
      milliseconds t = 0;

      while (t <= sweepDuration)
      {
        // Always sweep clockwise from 0 to full circle + rampWidth
        thetaLead = map(t, 0, sweepDuration, 0, FULL_CIRCLE + rampWidth);
        thetaTail = thetaLead - rampWidth;

        FOR_EACH_STRIP
        {
          FOR_EACH_LED
          {
            unsigned short theta = STRIPS[iStrip].leds[iLed].polar.cdegrees;

            // Simple clockwise range check with wraparound
            bool inRange;
            if (thetaLead > FULL_CIRCLE)
            {
              // Handle wraparound at the end of sweep
              inRange = (theta >= thetaTail) || (theta <= (thetaLead - FULL_CIRCLE));
            }
            else if (thetaTail < 0)
            {
              // Handle negative tail at the beginning of sweep
              inRange = (theta >= (thetaTail + FULL_CIRCLE)) || (theta <= thetaLead);
            }
            else
            {
              // Normal case - no wraparound
              inRange = (theta >= thetaTail && theta <= thetaLead);
            }

            if (inRange)
            {
              // Calculate brightness based on distance from leading edge
              short rampDistance;
              if (thetaLead > FULL_CIRCLE && theta <= (thetaLead - FULL_CIRCLE))
              {
                // LED is in the wrapped portion
                rampDistance = (thetaLead - FULL_CIRCLE) - theta;
              }
              else if (thetaTail < 0 && theta >= (thetaTail + FULL_CIRCLE))
              {
                // LED is in the wrapped portion (negative tail case)
                rampDistance = thetaLead - (theta - FULL_CIRCLE);
              }
              else
              {
                // Normal case
                rampDistance = thetaLead - theta;
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
// SequentialFadeIn - Internal Animation Class
// ============================================================================

// Fade in each strip with a random color
class SequentialFadeIn : public AbstractAnimation
{
  public:
    virtual const char* GetName() { return "SequentialFadeIn"; }

    RandParam<milliseconds, 150, 500> fadeIn;
    milliseconds fadeOut = 2 * fadeIn;

    void run() override
    {
      paint(CRGB::Black);

      // Shuffle the strip indices to randomize the order of fading in
      int strips[NUM_STRIPS];
      for (int i = 0; i < NUM_STRIPS; i++) strips[i] = i;
      shuffle(strips, NUM_STRIPS);

      for (byte i = 0; i < NUM_STRIPS; i++)
      {
        fadeInStrip(strips[i], randomColor(), fadeIn);
      }

      milliseconds start = millis();
      milliseconds dt;
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
  milliseconds flashDuration = 250;
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
  byte ANIMATIONS_COUNT = 4;

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
      return new Swipe();
    default:
      return new ErrorAnimation();
  }
}