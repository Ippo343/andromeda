#include "geometry/model_config.h"
#include "geometry/geometry.h"
#include <FastLED.h>

namespace SingleStripTestDevice {

const uint8_t NUM_STRIPS = 1;
const PROGMEM uint8_t STRIP_LENGTHS[NUM_STRIPS] = { 56 };
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

const PROGMEM PolarCoordinates STRIP_POLAR[] = {
    {458, 0}, {442, 0}, {425, 0}, {408, 0}, {392, 0}, {375, 0}, {358, 0}, {342, 0},
    {325, 0}, {308, 0}, {292, 0}, {275, 0}, {258, 0}, {242, 0}, {225, 0}, {208, 0},
    {192, 0}, {175, 0}, {158, 0}, {142, 0}, {125, 0}, {108, 0}, {92, 0},  {75, 0},
    {58, 0},  {42, 0},  {25, 0},  {8, 0},   {8, 18000}, {25, 18000}, {42, 18000}, {58, 18000},
    {75, 18000}, {92, 18000}, {108, 18000}, {125, 18000}, {142, 18000}, {158, 18000}, {175, 18000}, {192, 18000},
    {208, 18000}, {225, 18000}, {242, 18000}, {258, 18000}, {275, 18000}, {292, 18000}, {308, 18000}, {325, 18000},
    {342, 18000}, {358, 18000}, {375, 18000}, {392, 18000}, {408, 18000}, {425, 18000}, {442, 18000}, {458, 18000}
};

extern const ModelConfig CONFIG = {
    .id = ModelId::SINGLE_STRIP_TEST_DEVICE,
    .name = "Single Strip Test Rig",

    .num_strips = NUM_STRIPS,
    .strip_lengths = STRIP_LENGTHS,
    .pin_map = PIN_MAP,

    .screen_size_mm = 934,
    .cartesian_data = (const CartesianCoordinates*)STRIP_CARTESIAN,
    .polar_data = (const PolarCoordinates*)STRIP_POLAR,
};

} // namespace SingleStripTestRig