#pragma once

// DVD-logo aesthetic: a single small, tight ball drifts in straight lines across
// an otherwise black frame, bouncing off the four walls, its hue drifting slowly
// and continuously as it goes. The ball lights only the handful of LEDs it is
// passing close to (a compact finite-radius kernel, not the long inverse-square
// tail), so the rest of the frame stays dark. The instant it strikes a wall the
// LED nearest the impact point flashes white for as long as the ball is "in
// contact".
//
// Not an EmitterFieldEffect render: it reuses BoxBounce for motion but overrides
// evaluate() with its own hard-edged kernel + the wall-contact flash. Still no
// ROTATE_SPACE - the box is axis-aligned so a square device is struck head-on.

#include <math.h>

#include "effects-base.h"
#include "effects-utils.h"
#include "geometry/geometry.h"
#include "physics/box-bounce.h"
#include "physics/frame-clock.h"
#include "utils.h"

class BouncingBallGlow : public AbstractEffect
{
   public:
    const char* GetName() override { return "Bouncing Ball Glow"; }

    BouncingBallGlow()
    {
        box_.initRandom(1, (float)GEOMETRY.getScreenHalfWidth(),
                        (float)GEOMETRY.getScreenHalfHeight(), MIN_SPEED_MM_S, MAX_SPEED_MM_S);
        box_.setBounceJitter((float)(uint8_t)bounceJitterDeg_ * (PI / 180.0f));
    }

    void precompute(milliseconds_t t) override
    {
        milliseconds_t dt = clock_.tick(t);
        box_.step(dt);
        rebuildFlash();

        // Continuous hue drift: derived straight from absolute t via a shift (same
        // idiom as AngularPaletteRotation), so it's smooth, frame-rate independent
        // and can't overflow. hueShift_ 7..10 -> a full colour wheel every
        // ~33 .. 262 s.
        uint8_t hue = (uint8_t)startHue_ + (uint8_t)(t >> (uint8_t)hueShift_);
        ballColor_ = CHSV(hue, 255, 255);
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        float dx = (float)led->cartesian.x - box_.pos[0].x;
        float dy = (float)led->cartesian.y - box_.pos[0].y;
        float q = (dx * dx + dy * dy) / (RADIUS_MM * RADIUS_MM);

        CRGB out = CRGB::Black;
        if (q < 1.0f)
        {
            float k = 1.0f - q;
            out = ballColor_;
            out.nscale8((uint8_t)(255.0f * k * k));  // smooth, hard cutoff at RADIUS_MM
        }

        if (flashLevel_ && strip->idx == flashStrip_ && led_idx == flashLed_)
            out += CRGB(flashLevel_, flashLevel_, flashLevel_);
        return out;
    }

   protected:
    static constexpr float MIN_SPEED_MM_S = 40.0f;
    static constexpr float MAX_SPEED_MM_S = 110.0f;
    // Glow radius in mm: a couple of LED pitches, so the ball lights only its
    // immediate neighbourhood and everything past RADIUS_MM is truly black.
    RandParam<uint8_t, 18, 34> radiusMm_;
    // Right-shift applied to t to get the drifting hue (larger = slower).
    RandParam<uint8_t, 7, 10> hueShift_;
    // Random heading kick applied on every wall bounce, in degrees - just enough
    // that the ball wanders instead of tracing one fixed rectangular orbit.
    RandParam<uint8_t, 4, 12> bounceJitterDeg_;
    RandParam<uint8_t, 0, 255> startHue_;
    // How long, in ms, the white wall-contact flash lingers after a bounce.
    static constexpr float FLASH_MS = 130.0f;

   private:
    // Paint the LED nearest the ball's most recent wall impact white, fading out
    // over FLASH_MS. At most one LED is ever lit at a time, so instead of a
    // per-strip vector zeroed and rescanned in full every frame, track just
    // that one {strip, led, level} triple. The nearest-LED scan itself only
    // needs to run on the frame a bounce actually happens - BoxBounce resets
    // sinceHit[0] to exactly 0.0f on that frame - since the impact point is
    // fixed between bounces and only the fade level changes frame to frame.
    void rebuildFlash()
    {
        float ageMs = box_.sinceHit[0] * 1000.0f;
        if (ageMs >= FLASH_MS)
        {
            flashLevel_ = 0;
            return;
        }

        if (box_.sinceHit[0] == 0.0f)
        {
            float hx = box_.hitPoint[0].x;
            float hy = box_.hitPoint[0].y;
            float bestD2 = 1e18f;
            FOR_EACH_STRIP
            {
                LedStrip& s = GEOMETRY.getStrip(iStrip);
                for (size_t l = 0; l < s.num_leds; l++)
                {
                    float dx = (float)s.leds[l].cartesian.x - hx;
                    float dy = (float)s.leds[l].cartesian.y - hy;
                    float d2 = dx * dx + dy * dy;
                    if (d2 < bestD2)
                    {
                        bestD2 = d2;
                        flashStrip_ = iStrip;
                        flashLed_ = l;
                    }
                }
            }
        }

        flashLevel_ = (uint8_t)(255.0f * (1.0f - ageMs / FLASH_MS));
    }

    // RADIUS_MM is fixed for the instance once radiusMm_ is drawn.
    const float RADIUS_MM = (float)(uint8_t)radiusMm_;

    BoxBounce box_;
    CRGB ballColor_ = CRGB::White;
    size_t flashStrip_ = 0;
    size_t flashLed_ = 0;
    uint8_t flashLevel_ = 0;
    FrameClock clock_;

#ifdef UNIT_TEST
   public:
    // Test-only: the motion state, so native tests can read the ball position and
    // assert it stays inside the render area without a public runtime accessor.
    const BoxBounce& boxForTest() const { return box_; }
#endif
};
