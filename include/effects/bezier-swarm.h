#pragma once

// Each emitter follows its own independent, indefinitely self-extending cubic Bezier
// path (see physics/bezier-path.h for the curve/motion math itself).

#include <math.h>

#include <vector>

#include "control-hints.h"
#include "effects-utils.h"
#include "effects/emitter-field-effect.h"
#include "physics/bezier-path.h"
#include "physics/physics-random.h"
#include "utils.h"

using std::vector;

class BezierSwarm : public EmitterFieldEffect
{
   public:
    const char* GetName() override { return "Bezier Swarm"; }

    vector<BezierPath> paths;

    // BezierSwarm is the only emitter-field effect whose range reaches N=1
    // (MultiPendulum starts at 2, RGBodyProblem at 3). A lone emitter with a fixed hue
    // would look like a static-colored dot drifting around forever, so it instead gets
    // a slow, frame-rate-independent, continuously rotating hue - simpler than the old
    // RGBodyProblem's MoodLight (independently-oscillating RGB channels) and stays
    // fully saturated/vivid at every instant instead of occasionally desaturating.
    // N>=2 keeps fixed complementary colors: ongoing relative motion between multiple
    // emitters already supplies visual variety there.
    static constexpr float HUE_DRIFT_DEG_PER_SECOND = 12.0f;
    float hueDeg = 0;

    BezierSwarm() : EmitterFieldEffect(RandParam<int, 1, 6>()), paths(positions.size())
    {
        controlHints = ControlHints::ROTATE_SPACE;

        if (positions.size() == 1)
        {
            hueDeg = randomFloat(0.0f, 360.0f);
            colors[0] = hueToColor(hueDeg);
        }
        else { colors = randomComplementaryColors((int)positions.size()); }

        syncPositions([this](size_t i) { return paths[i].position(); });
    }

    void updatePositions(milliseconds_t t, milliseconds_t dt) override
    {
        for (auto& path : paths) path.step(dt);
        syncPositions([this](size_t i) { return paths[i].position(); });
    }

    void updateColors(milliseconds_t t, milliseconds_t dt) override
    {
        if (positions.size() != 1) return;
        hueDeg = fmodf(hueDeg + HUE_DRIFT_DEG_PER_SECOND * dt / 1000.0f, 360.0f);
        colors[0] = hueToColor(hueDeg);
    }

   private:
    static CHSV hueToColor(float degrees)
    {
        return CHSV((uint8_t)(degrees / 360.0f * 255.0f), 255, 255);
    }
};
