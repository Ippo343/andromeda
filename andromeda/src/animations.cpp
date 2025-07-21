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
// SweepLoops - Internal Animation Class
// ============================================================================

// Pretty much the same as SweepStrips, but using all the loops instead
class SweepLoops : public AbstractAnimation
{
  public:
    virtual const char* GetName() { return "SweepLoops"; }
    
    RandParam<byte, 50, 100> timeStep;
    
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
      FOR_EACH_STRIP
      {
        paintStrip(iStrip, color);
        FastLED.show();
        delay(timeStep);
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

      FOR_EACH_STRIP
      {
        fadeInStrip(iStrip, randomColor(), fadeIn);
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
      return new SweepLoops();
    case 3:
      return new Swipe();
    default:
      return new ErrorAnimation();
  }
}