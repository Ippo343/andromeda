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

// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect();
