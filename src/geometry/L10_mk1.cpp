#include <FastLED.h>

#include "geometry/geometry.h"
#include "geometry/model_config.h"

namespace L10_MK1
{
// Single 56-LED strip: a chamfered square, 13 LEDs per side plus one corner
// LED at each of the four corners. Coordinates in whole mm, origin at centre
// (rounded from the supplied CSV; panel spans +-51.455 mm on both axes).

const uint8_t NUM_STRIPS = 1;
const PROGMEM size_t STRIP_LENGTHS[NUM_STRIPS] = {56};
const PROGMEM uint8_t PIN_MAP[NUM_STRIPS] = {1};

// clang-format off
const PROGMEM CartesianCoordinates coords_L10_MK1[56] = {
  { -42, -51 }, { -35, -51 }, { -28, -51 }, { -21, -51 }, { -14, -51 }, {  -7, -51 }, {   0, -51 }, {   7, -51 }, {  14, -51 }, {  21, -51 }, {  28, -51 }, {  35, -51 }, {  42, -51 },
  {  47, -47 },
  {  51, -42 }, {  51, -35 }, {  51, -28 }, {  51, -21 }, {  51, -14 }, {  51,  -7 }, {  51,   0 }, {  51,   7 }, {  51,  14 }, {  51,  21 }, {  51,  28 }, {  51,  35 }, {  51,  42 },
  {  47,  47 },
  {  42,  51 }, {  35,  51 }, {  28,  51 }, {  21,  51 }, {  14,  51 }, {   7,  51 }, {   0,  51 }, {  -7,  51 }, { -14,  51 }, { -21,  51 }, { -28,  51 }, { -35,  51 }, { -42,  51 },
  { -47,  47 },
  { -51,  42 }, { -51,  35 }, { -51,  28 }, { -51,  21 }, { -51,  14 }, { -51,   7 }, { -51,   0 }, { -51,  -7 }, { -51, -14 }, { -51, -21 }, { -51, -28 }, { -51, -35 }, { -51, -42 },
  { -47, -47 }
};
// clang-format on

extern const ModelConfig CONFIG = {
    .id = ModelId::L10_MK1,
    .name = "L10 MK1",

    .num_strips = NUM_STRIPS,
    .strip_lengths = STRIP_LENGTHS,
    .pin_map = PIN_MAP,

    .screen_height_mm = 103,
    .screen_width_mm = 103,
    .cartesian_data = (const CartesianCoordinates*)coords_L10_MK1,

    .min_frame_duration_ms = 7,  // 7ms = ~142fps

    // Same cell, boost converter and 5V LED rail as L10 MK0 - see the budget
    // derivation in L10_mk0.cpp (#216).
    .max_milliamps = 650,
};

}  // namespace L10_MK1
