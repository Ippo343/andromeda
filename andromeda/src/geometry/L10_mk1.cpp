#include <FastLED.h>

#include "geometry/geometry.h"
#include "geometry/model_config.h"

namespace L10_MK1
{
// TODO: these is all tentative right now

const uint8_t NUM_STRIPS = 1;
const PROGMEM size_t STRIP_LENGTHS[NUM_STRIPS] = {52};
const PROGMEM uint8_t PIN_MAP[NUM_STRIPS] = {1};

// clang-format off
const PROGMEM CartesianCoordinates coords_L10[52] = {
  {   42,   42 }, {   35,   42 }, {   28,   42 }, {   21,   42 }, {   14,   42 }, {    7,   42 }, {    0,   42 }, {   -7,   42 }, {  -14,   42 }, {  -21,   42 }, {  -28,   42 }, {  -35,   42 }, {  -42,   42 },
  {  -42,   42 }, {  -42,   35 }, {  -42,   28 }, {  -42,   21 }, {  -42,   14 }, {  -42,    7 }, {  -42,    0 }, {  -42,   -7 }, {  -42,  -14 }, {  -42,  -21 }, {  -42,  -28 }, {  -42,  -35 }, {  -42,  -42 },
  {  -42,  -42 }, {  -35,  -42 }, {  -28,  -42 }, {  -21,  -42 }, {  -14,  -42 }, {   -7,  -42 }, {    0,  -42 }, {    7,  -42 }, {   14,  -42 }, {   21,  -42 }, {   28,  -42 }, {   35,  -42 }, {   42,  -42 },
  {   42,  -42 }, {   42,  -35 }, {   42,  -28 }, {   42,  -21 }, {   42,  -14 }, {   42,   -7 }, {   42,    0 }, {   42,    7 }, {   42,   14 }, {   42,   21 }, {   42,   28 }, {   42,   35 }, {   42,   42 }
};
// clang-format on

extern const ModelConfig CONFIG = {
    .id = ModelId::L10_MK1,
    .name = "L10 MK1",

    .num_strips = NUM_STRIPS,
    .strip_lengths = STRIP_LENGTHS,
    .pin_map = PIN_MAP,

    .screen_height_mm = 84,
    .screen_width_mm = 84,
    .cartesian_data = (const CartesianCoordinates*)coords_L10,

    .max_cpu_freq_mhz = 80,

    .min_frame_duration_ms = 7,  // 7ms = ~142fps
};

}  // namespace L10_MK1