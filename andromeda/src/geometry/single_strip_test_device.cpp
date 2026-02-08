#include "geometry/model_config.h"
#include "geometry/geometry.h"
#include <FastLED.h>

namespace SingleStripTestDevice {

const uint8_t NUM_STRIPS = 1;
const PROGMEM size_t STRIP_LENGTHS[NUM_STRIPS] = { 56 };
const PROGMEM uint8_t PIN_MAP[NUM_STRIPS] = { 1 };

const PROGMEM CartesianCoordinates STRIP_CARTESIAN[] = {
    {458, 0}, {442, 0}, {425, 0}, {408, 0}, {392, 0}, {375, 0}, {358, 0}, {342, 0},
    {325, 0}, {308, 0}, {292, 0}, {275, 0}, {258, 0}, {242, 0}, {225, 0}, {208, 0},
    {192, 0}, {175, 0}, {158, 0}, {142, 0}, {125, 0}, {108, 0}, {92, 0},  {75, 0},
    {58, 0},  {42, 0},  {25, 0},  {8, 0},   {-8, 0},   {-25, 0},  {-42, 0},  {-58, 0},
    {-75, 0},  {-92, 0},  {-108, 0}, {-125, 0}, {-142, 0}, {-158, 0}, {-175, 0}, {-192, 0},
    {-208, 0}, {-225, 0}, {-242, 0}, {-258, 0}, {-275, 0}, {-292, 0}, {-308, 0}, {-325, 0},
    {-342, 0}, {-358, 0}, {-375, 0}, {-392, 0}, {-408, 0}, {-425, 0}, {-442, 0}, {-458, 0}
};


extern const ModelConfig CONFIG = {
    .id = ModelId::SINGLE_STRIP_TEST_DEVICE,
    .name = "Single Strip Test Rig",

    .num_strips = NUM_STRIPS,
    .strip_lengths = STRIP_LENGTHS,
    .pin_map = PIN_MAP,

    .screen_size_mm = 934,
    .cartesian_data = (const CartesianCoordinates*)STRIP_CARTESIAN,
};

} // namespace SingleStripTestRig