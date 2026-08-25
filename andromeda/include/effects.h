#pragma once

#include "effects-base.h"
#include "effects/cartesian-moodlight.h"
#include "effects/electric-sparks.h"
#include "effects/error-effect.h"
#include "effects/hexagonal-ripple-galaxy.h"
#include "effects/individual-strip-drift.h"
#include "effects/individual-strip-moodlight.h"
#include "effects/ninja-star.h"
#include "effects/palette-wave.h"
#include "effects/polar-moodlight.h"
#include "effects/polar-swipe.h"
#include "effects/rgbody-problem.h"
#include "effects/saturation-glow.h"
#include "effects/static-color.h"

// Stable identity for every rotation-eligible effect, so it can be referred
// to by id over the wire (web UI effect selection) instead of only by its
// GetName() string. Listed alphabetically - no significance to the order
// beyond giving the web dropdown a sensible default order for free.
// StaticColor and ErrorEffect are deliberately excluded: StaticColor is only
// reachable via the COLOR command's own path (transitionToStaticColor()),
// and ErrorEffect is an internal fallback, not a user-selectable effect.
enum class EffectId : uint8_t
{
    CartesianMoodlight,
    ElectricSparks,
    HexagonalRippleGalaxy,
    IndividualStripDrift,
    IndividualStripMoodlight,
    NinjaStar,
    PaletteWave,
    PolarMoodlight,
    PolarSwipe,
    RGBodyProblem,
    SaturationGlow,
};

struct EffectInfo
{
    EffectId id;
    const char* name;
};

// Global registry of all rotation-eligible effects, in EffectId order.
extern const EffectInfo EFFECT_REGISTRY[];
extern const size_t NUM_EFFECTS;

// Instantiates a fresh effect for the given id.
AbstractEffect* createEffect(EffectId id);

// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect();
