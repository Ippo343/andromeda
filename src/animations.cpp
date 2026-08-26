#include "animations.h"

#include "animation-utils.h"
#include "segmented-animation.h"

// ============================================================================
// SweepStrips - Internal Animation Class
// ============================================================================

// Sweeps all the strips with random colors, then white and then black
// sequentially. Each color is one segment; within a segment, the number of
// lit LEDs is a function of how far into the segment we are (segmentT)
// instead of a discrete step counter driven by delay().
class SweepStrips : public SegmentedAnimation
{
   public:
    virtual const char* GetName() { return "SweepStrips"; }

    SweepStrips()
    {
        vector<CHSV> colors = randomComplementaryColors(3);
        for (auto& color : colors) addColorSegment(color);
        addColorSegment(CRGB::White);
        addColorSegment(CRGB::Black);
    }

   private:
    // A fixed total duration per segment - not scaled by LED count like the old
    // ms-per-LED timeStep was - so a full sweep takes the same time regardless of
    // strip length instead of one segment alone taking tens of seconds on a
    // 1600-LED grid. Matches the range ClockSweep/RadialSweep already use for the
    // same "one sweep, however many LEDs" duration.
    RandParam<milliseconds_t, 400, 800> segmentDuration;

    void addColorSegment(CRGB color)
    {
        // Find the longest strip
        size_t maxLeds = 1;
        for (size_t i = 0; i < GEOMETRY.getNumStrips(); i++)
        {
            maxLeds = max(maxLeds, GEOMETRY.getStrip(i).num_leds);
        }

        milliseconds_t duration = segmentDuration;

        addSegment(duration,
                   [maxLeds, duration, color](milliseconds_t segmentT)
                   {
                       // SegmentedAnimation only ever calls a non-final segment's fn
                       // with segmentT strictly less than its duration (it hands the
                       // boundary tick to the *next* segment instead - see
                       // segmented-animation.h) - mapping straight against the full
                       // duration therefore always undershoots by up to one frame's
                       // worth of LEDs. Invisible on a short strip, but a real dark
                       // gap at the tail on a long one (e.g. the 1600-LED grid test
                       // rig). Map against 90% of duration instead, clamped, so the
                       // sweep visibly finishes with margin to spare well before the
                       // segment's real end - the same headroom BaseSweep gets for
                       // free from its brightness ramp width.
                       milliseconds_t effectiveDuration = max(duration * 9 / 10, (milliseconds_t)1);
                       size_t step =
                           min((size_t)map(segmentT, 0, effectiveDuration, 0, maxLeds - 1),
                               maxLeds - 1);

                       FOR_EACH_STRIP
                       {
                           size_t stripLen = GEOMETRY.getStrip(iStrip).num_leds;
                           size_t ledsToLight = map(step, 0, maxLeds - 1, 0, stripLen - 1);

                           // Fill from 0 to ledsToLight
                           for (size_t i = 0; i <= ledsToLight; i++)
                           {
                               GEOMETRY.getStrip(iStrip).buffer[i] = color;
                           }
                       }
                   });
    }
};

// ============================================================================
// BaseSweep - Abstract Base Class for Sweep Animations
// ============================================================================

class BaseSweep : public SegmentedAnimation
{
   protected:
    bool direction;  // Subclasses define meaning (clockwise/outward, etc.)
    unsigned short sweepDuration;
    unsigned short rampWidth;

    // Pure virtual functions that subclasses must implement
    virtual void flipCoordinates() = 0;
    virtual unsigned short getCoordinate(int strip, int led) = 0;
    virtual unsigned short getMaxCoordinate() = 0;

    // Called from each concrete subclass's constructor, once direction/
    // sweepDuration/rampWidth are set - not from BaseSweep's own constructor,
    // so the virtual calls below (getMaxCoordinate/getCoordinate/
    // flipCoordinates) resolve to the derived class's overrides.
    void buildSegments()
    {
        vector<CHSV> colors = randomComplementaryColors(3);

        if (!direction) { flipCoordinates(); }

        for (auto& color : colors) addSweepSegment(color);
        addSweepSegment(CRGB::White);
        addSweepSegment(CRGB::Black);
    }

   private:
    void addSweepSegment(CRGB color)
    {
        unsigned short maxCoord = getMaxCoordinate();
        unsigned short ramp = rampWidth;
        unsigned short duration = sweepDuration;

        addSegment(duration,
                   [this, color, maxCoord, ramp, duration](milliseconds_t segmentT)
                   {
                       // Always sweep in positive direction from 0 to max + rampWidth
                       long coordLead = map(segmentT, 0, duration, 0, maxCoord + ramp);
                       long coordTail = coordLead - ramp;

                       FOR_EACH_STRIP
                       {
                           FOR_EACH_LED(iStrip)
                           {
                               unsigned short coord = getCoordinate(iStrip, iLed);

                               SweepRampResult sweepRamp =
                                   computeSweepRamp(coordLead, coordTail, coord, maxCoord, ramp);
                               if (sweepRamp.inRange)
                               {
                                   uint8_t brightness =
                                       map(sweepRamp.rampDistance, 0, ramp, 0, 255);
                                   GEOMETRY.getStrip(iStrip).buffer[iLed] = color % brightness;
                               }
                           }
                       }
                   });
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
        buildSegments();
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
        buildSegments();
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

// Fade in each strip with a random color, one strip at a time, then fade all
// of them out together. Converted to N per-strip fade-in segments (each
// duration `fadeIn`, matching the old sequential fadeInStrip() calls) plus a
// final group fade-out segment (duration `fadeOut`).
class SequentialFadeIn : public SegmentedAnimation
{
   public:
    virtual const char* GetName() { return "SequentialFadeIn"; }

    SequentialFadeIn()
    {
        paint(CRGB::Black);

        size_t numStrips = GEOMETRY.getNumStrips();
        milliseconds_t fadeInMs = fadeIn;
        milliseconds_t fadeOutMs = fadeOut;

        // Shuffle the strip indices to randomize the order of fading in
        vector<int> strips(numStrips);
        for (size_t i = 0; i < numStrips; i++) strips[i] = i;
        shuffle(strips.data(), (int)numStrips);

        vector<CHSV> colors = randomComplementaryColors((int)numStrips);

        for (size_t i = 0; i < numStrips; i++)
        {
            int stripIdx = strips[i];
            CHSV color = colors[i];
            addSegment(fadeInMs,
                       [stripIdx, color, fadeInMs](milliseconds_t segmentT)
                       {
                           uint8_t v = constrain(map(segmentT, 0, fadeInMs, 0, color.v), 0, 255);
                           paintStrip(stripIdx, CHSV(color.h, color.s, v));
                       });
        }

        addSegment(fadeOutMs,
                   [strips, colors, numStrips, fadeOutMs](milliseconds_t segmentT)
                   {
                       uint8_t b = constrain(map(segmentT, 0, fadeOutMs, 255, 0), 0, 255);
                       for (size_t i = 0; i < numStrips; i++)
                       {
                           paintStrip(strips[i], colors[i] % b);
                       }
                   });
    }

   private:
    RandParam<milliseconds_t, 150, 500> fadeIn;
    milliseconds_t fadeOut = 2 * fadeIn;
};

// ============================================================================
// Swipe - Internal Animation Class
// ============================================================================

// Swipes a random color from left to right, fading the trail out behind it.
// The original had no explicit duration (it stepped a spatial variable in a
// tight loop with no delay(), so its wall-clock length was just however long
// the loop took to run); converting to a per-frame model requires making
// that duration explicit, similar to the other sweeps.
class Swipe : public SegmentedAnimation
{
   public:
    virtual const char* GetName() { return "Swipe"; }

    Swipe()
    {
        controlHints |= ControlHints::ROTATE_SPACE;

        paint(CRGB::Black);
        CRGB color = randomColor();
        short radius = GEOMETRY.getScreenRadius();
        short stepLocal = step;
        milliseconds_t durationMs = duration;

        // Goes to (step * radius) so the fading trail has time to fully fade out.
        short vStart = -radius;
        short vEnd = stepLocal * radius;

        // Trail fade rate, expressed as a fraction of energy lost per second instead of a
        // fixed amount per renderFrame() call, so the trail length in real time doesn't
        // depend on how often renderFrame() happens to be called - see accumulateFadeAmount().
        // Starting point equivalent to the old fixed stepLocal-out-of-255 fade at a ~60fps
        // reference; needs a visual pass on real hardware to retune.
        constexpr float TRAIL_LOSS_RATE_PER_SECOND = 0.6f;

        addSegment(
            durationMs,
            [color, stepLocal, vStart, vEnd, durationMs, TRAIL_LOSS_RATE_PER_SECOND,
             lastSegmentT = (milliseconds_t)-1, decayDebt = 0.0f](milliseconds_t segmentT) mutable
            {
                milliseconds_t dt =
                    (lastSegmentT == (milliseconds_t)-1) ? 16 : segmentT - lastSegmentT;
                lastSegmentT = segmentT;

                short v = map(segmentT, 0, durationMs, vStart, vEnd);
                uint8_t fadeAmount =
                    accumulateFadeAmount(decayDebt, TRAIL_LOSS_RATE_PER_SECOND, dt);

                FOR_EACH_STRIP
                {
                    FOR_EACH_LED(iStrip)
                    {
                        short lv = GEOMETRY.getStrip(iStrip).leds[iLed].cartesian.x;
                        if (lv >= (v - stepLocal) && lv <= v)
                            GEOMETRY.getStrip(iStrip).buffer[iLed] = color;
                    }

                    fadeToBlackBy(GEOMETRY.getStrip(iStrip).buffer,
                                  GEOMETRY.getStrip(iStrip).num_leds, fadeAmount);
                }
            });
    }

   private:
    // It looks smoother if the swept edge advances in steps of 2 or 3.
    RandParam<short, 2, 3> step;
    RandParam<milliseconds_t, 400, 700> duration;
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

AbstractFrameAnimation* getRandomAnimation()
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
        default:
            return new Swipe();
    }
}