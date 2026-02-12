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
  {   42,   47 }, {   35,   47 }, {   28,   47 }, {   21,   47 }, {   14,   47 }, {    7,   47 }, {    0,   47 }, {   -7,   47 }, {  -14,   47 }, {  -21,   47 }, {  -28,   47 }, {  -35,   47 }, {  -42,   47 },
  {  -47,   42 }, {  -47,   35 }, {  -47,   28 }, {  -47,   21 }, {  -47,   14 }, {  -47,    7 }, {  -47,    0 }, {  -47,   -7 }, {  -47,  -14 }, {  -47,  -21 }, {  -47,  -28 }, {  -47,  -35 }, {  -47,  -42 },
  {  -42,  -47 }, {  -35,  -47 }, {  -28,  -47 }, {  -21,  -47 }, {  -14,  -47 }, {   -7,  -47 }, {    0,  -47 }, {    7,  -47 }, {   14,  -47 }, {   21,  -47 }, {   28,  -47 }, {   35,  -47 }, {   42,  -47 },
  {   47,  -42 }, {   47,  -35 }, {   47,  -28 }, {   47,  -21 }, {   47,  -14 }, {   47,   -7 }, {   47,    0 }, {   47,    7 }, {   47,   14 }, {   47,   21 }, {   47,   28 }, {   47,   35 }, {   47,   42 }
};
// clang-format on

extern const ModelConfig CONFIG = {
    .id = ModelId::L10_MK1,
    .name = "L10 MK1",

    .num_strips = NUM_STRIPS,
    .strip_lengths = STRIP_LENGTHS,
    .pin_map = PIN_MAP,

    .screen_height_mm = 94,
    .screen_width_mm = 94,
    .cartesian_data = (const CartesianCoordinates*)coords_L10,

    .max_cpu_freq_mhz = 80,
};

}  // namespace L10_MK1