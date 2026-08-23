#include "animations.h"

#include "animation-utils.h"

// ============================================================================
// SweepStrips - Internal Animation Class
// ============================================================================

// Sweeps all the strips with random colors,
// then white and then black sequentially
class SweepStrips : public AbstractBlockingAnimation
{
   public:
    virtual const char* GetName() { return "SweepStrips"; }

    RandParam<int, 10, 30> timeStep;

    void run() override
    {
        vector<CHSV> colors = randomComplementaryColors(3);

        paint(CRGB::Black);
        for (size_t c = 0; c < 3; c++) colorSweep(colors[c]);
        colorSweep(CRGB::White);
        colorSweep(CRGB::Black);
    }

   private:
    void colorSweep(CRGB color)
    {
        // Find the longest strip
        size_t maxLeds = 0;
        for (size_t i = 0; i < GEOMETRY.getNumStrips(); i++)
        {
            maxLeds = max(maxLeds, GEOMETRY.getStrip(i).num_leds);
        }

        // Sweep with normalized progress
        for (size_t step = 0; step < maxLeds; step++)
        {
            FOR_EACH_STRIP
            {
                size_t stripLen = GEOMETRY.getStrip(iStrip).num_leds;

                // Map the current step to how many LEDs should be lit on this strip
                size_t ledsToLight = map(step, 0, maxLeds - 1, 0, stripLen - 1);

                // Fill from 0 to ledsToLight
                for (size_t i = 0; i <= ledsToLight; i++)
                {
                    GEOMETRY.getStrip(iStrip).buffer[i] = color;
                }
            }
            FASTLED_SHOW();
            delay(timeStep);
        }
    }
};

// ============================================================================
// BaseSweep - Abstract Base Class for Sweep Animations
// ============================================================================

class BaseSweep : public AbstractBlockingAnimation
{
   protected:
    bool direction;  // Subclasses define meaning (clockwise/outward, etc.)
    unsigned short sweepDuration;
    unsigned short rampWidth;

    // Pure virtual functions that subclasses must implement
    virtual void flipCoordinates() = 0;
    virtual unsigned short getCoordinate(int strip, int led) = 0;
    virtual unsigned short getMaxCoordinate() = 0;

    void run() override
    {
        vector<CHSV> colors = randomComplementaryColors(3);

        // Coordinate system flip for direction (if needed)
        if (!direction) { flipCoordinates(); }

        paint(CRGB::Black);
        for (size_t c = 0; c < 3; c++) { colorSweep(colors[c]); }
        colorSweep(CRGB::White);
        colorSweep(CRGB::Black);
    }

   private:
    void colorSweep(CRGB color)
    {
        long coordLead = 0;  // The coordinate of the leading edge of the ramp
        long coordTail = 0;  // The coordinate of the trailing edge of the ramp
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
                FOR_EACH_LED(iStrip)
                {
                    unsigned short coord = getCoordinate(iStrip, iLed);

                    SweepRampResult ramp =
                        computeSweepRamp(coordLead, coordTail, coord, maxCoord, rampWidth);
                    if (ramp.inRange)
                    {
                        uint8_t brightness = map(ramp.rampDistance, 0, rampWidth, 0, 255);
                        GEOMETRY.getStrip(iStrip).buffer[iLed] = color % brightness;
                    }
                }
            }

            FASTLED_SHOW();
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
        rampWidth = 1000;  // 10 degrees
    }

   protected:
    void flipCoordinates() override
    {
        FOR_EACH_STRIP
        {
            FOR_EACH_LED(iStrip)
            {
                GEOMETRY.getStrip(iStrip).leds[iLed].polar.cdegrees =
                    FULL_CIRCLE - GEOMETRY.getStrip(iStrip).leds[iLed].polar.cdegrees;
            }
        }
    }

    unsigned short getCoordinate(int strip, int led) override
    {
        return GEOMETRY.getStrip(strip).leds[led].polar.cdegrees;
    }

    unsigned short getMaxCoordinate() override { return FULL_CIRCLE; }
};

// ============================================================================
// RadialSweep - Radial Sweep Animation
// ============================================================================

class RadialSweep : public BaseSweep
{
   public:
    virtual const char* GetName() { return "RadialSweep"; }

    RandBool outward;  // For external API compatibility
    RandParam<milliseconds_t, 300, 750> duration;

    RadialSweep()
    {
        direction = outward;
        sweepDuration = duration;
        rampWidth = 10;  // Radial ramp width in distance units
    }

   protected:
    void flipCoordinates() override
    {
        FOR_EACH_STRIP
        {
            FOR_EACH_LED(iStrip)
            {
                GEOMETRY.getStrip(iStrip).leds[iLed].polar.radius =
                    GEOMETRY.getScreenRadius() - GEOMETRY.getStrip(iStrip).leds[iLed].polar.radius;
            }
        }
    }

    unsigned short getCoordinate(int strip, int led) override
    {
        return GEOMETRY.getStrip(strip).leds[led].polar.radius;
    }

    unsigned short getMaxCoordinate() override { return GEOMETRY.getScreenRadius(); }
};

// ============================================================================
// SequentialFadeIn - Internal Animation Class
// ============================================================================

// Fade in each strip with a random color
class SequentialFadeIn : public AbstractBlockingAnimation
{
   public:
    virtual const char* GetName() { return "SequentialFadeIn"; }

    RandParam<milliseconds_t, 150, 500> fadeIn;
    milliseconds_t fadeOut = 2 * fadeIn;

    void run() override
    {
        paint(CRGB::Black);

        // Shuffle the strip indices to randomize the order of fading in
        int strips[GEOMETRY.getNumStrips()];
        for (size_t i = 0; i < GEOMETRY.getNumStrips(); i++) strips[i] = i;
        shuffle(strips, GEOMETRY.getNumStrips());

        auto colors = randomComplementaryColors(GEOMETRY.getNumStrips());

        for (size_t i = 0; i < GEOMETRY.getNumStrips(); i++)
        {
            fadeInStrip(strips[i], colors[i], fadeIn);
        }

        milliseconds_t start = millis();
        milliseconds_t dt;
        do {
            dt = millis() - start;
            uint8_t b = constrain(map(dt, 0, fadeOut, 255, 0), 0, 255);

            FOR_EACH_STRIP { paintStrip(strips[iStrip], colors[iStrip] % b); }

            FASTLED_SHOW();
        } while (dt < fadeOut);
    }
};

// ============================================================================
// Swipe - Internal Animation Class
// ============================================================================

// Swipes a random color from left to right and then fades it out
class Swipe : public AbstractBlockingAnimation
{
   public:
    virtual const char* GetName() { return "Swipe"; }

    Swipe() { controlHints |= ControlHints::ROTATE_SPACE; }

    void run() override
    {
        paint(CRGB::Black);
        CRGB color = randomColor();

        // Even without any delay, scrolling the whole screen size takes a long time
        // (which I am a bit suspicious of to be honest, but I guess calling it 520 times is a bit
        // much). It looks smoother if you increase in steps of 2 or 3
        RandParam<short, 2, 3> step;

        // It goes to (step * GEOMETRY.getScreenHalfSize()) so that the fading trail has time to
        // fully fade out
        for (short v = -GEOMETRY.getScreenRadius(); v <= (step * GEOMETRY.getScreenRadius());
             v += step)
        {
            FOR_EACH_STRIP
            {
                FOR_EACH_LED(iStrip)
                {
                    short lv = GEOMETRY.getStrip(iStrip).leds[iLed].cartesian.x;
                    if (lv >= (v - step) && lv <= v) GEOMETRY.getStrip(iStrip).buffer[iLed] = color;
                }

                fadeToBlackBy(GEOMETRY.getStrip(iStrip).buffer, GEOMETRY.getStrip(iStrip).num_leds,
                              step);
            }

            FASTLED_SHOW();
        }
    }
};

// ============================================================================
// WiFiConnectingAnimation - External Animation Class Implementation
// ============================================================================

const char* WiFiConnectingAnimation::GetName() { return "WiFiConnectingAnimation"; }

void WiFiConnectingAnimation::run()
{
    paint(CRGB::Black);
    paintStrip(0, CRGB::SteelBlue);
    FASTLED_SHOW();
}

// ============================================================================
// WiFiSuccessAnimation - External Animation Class Implementation
// ============================================================================

const char* WiFiSuccessAnimation::GetName() { return "WiFiSuccessAnimation"; }

void WiFiSuccessAnimation::run()
{
    paint(CRGB::Black);
    paintStrip(0, CRGB::Green);
    FASTLED_SHOW();
    delay(250);
}

// ============================================================================
// ErrorAnimation - External Animation Class Implementation
// ============================================================================

const char* ErrorAnimation::GetName() { return "ErrorAnimation"; }

void ErrorAnimation::run()
{
    milliseconds_t flashDuration = 250;
    paint(CRGB::Black);

    for (size_t i = 0; i < 3; i++)
    {
        paintStrip(0, CRGB::Red);
        FASTLED_SHOW();
        delay(flashDuration);

        paintStrip(0, CRGB::Black);
        FASTLED_SHOW();
        delay(flashDuration);
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

AbstractBlockingAnimation* getRandomAnimation()
{
    size_t ANIMATIONS_COUNT = 5;

    // Set this to the index of the animation you want to force while testing
    short forcedSelection = -1;

    static size_t previousSelection = 255;

    size_t selection;
    if (forcedSelection >= 0)
        selection = forcedSelection;
    else
        do selection = random(ANIMATIONS_COUNT);
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