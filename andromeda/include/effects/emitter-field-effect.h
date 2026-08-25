#pragma once

// Shared base for the emitter-field family of effects (BezierSwarm, MultiPendulum, the
// real N-body RGBodyProblem): N colored point emitters, additively blended per-LED via
// inverse-square falloff. This factors out what RGBodyProblem's evaluate() used to
// hand-roll, generalized to float positions and to N (no more single-emitter special
// case in the rendering path itself - see each subclass for how it handles N=1 color
// liveliness where relevant).

#include <vector>

#include "effects-base.h"
#include "effects-utils.h"
#include "physics/frame-clock.h"
#include "physics/vec2f.h"

using std::vector;

class EmitterFieldEffect : public AbstractEffect
{
   public:
    vector<Vec2f> positions;
    vector<CHSV> colors;
    FrameClock clock;

    explicit EmitterFieldEffect(size_t n) : positions(n), colors(n), cartesianPositions_(n) {}

    // Subclass hook: run one physics step and refresh `positions`.
    virtual void updatePositions(milliseconds_t t, milliseconds_t dt) = 0;

    // Optional subclass hook for effects with time-varying color (e.g. BezierSwarm's
    // single-emitter hue drift). No-op by default: most emitter-field effects keep
    // `colors` fixed after construction.
    virtual void updateColors(milliseconds_t t, milliseconds_t dt) {}

    void precompute(milliseconds_t t) override
    {
        milliseconds_t dt = clock.tick(t);
        updatePositions(t, dt);
        updateColors(t, dt);

        // positions[i] is constant for the whole frame - convert to CartesianCoordinates
        // once here instead of once per LED per emitter in evaluate().
        for (size_t i = 0; i < positions.size(); i++)
            cartesianPositions_[i] = positions[i].toCartesian();
    }

    CRGB evaluate(LedStrip* strip, Led* led, size_t led_idx, milliseconds_t t) override
    {
        CRGB finalColor = CRGB::Black;
        for (size_t i = 0; i < positions.size(); i++)
        {
            CRGB emitterColor = colors[i];
            uint8_t v = brightnessFromEmitter(led, cartesianPositions_[i]);
            emitterColor.nscale8(v);
            finalColor += emitterColor;
        }
        return finalColor;
    }

   private:
    vector<CartesianCoordinates> cartesianPositions_;
};
