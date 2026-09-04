#include <FastLED.h>

#include "geometry/geometry.h"
#include "geometry/model_config.h"

namespace SingleStripTestDevice
{

const uint8_t NUM_STRIPS = 1;
const PROGMEM size_t STRIP_LENGTHS[NUM_STRIPS] = {56};
const PROGMEM uint8_t PIN_MAP[NUM_STRIPS] = {1};

const PROGMEM CartesianCoordinates STRIP_CARTESIAN[] = {
    {458, 0},  {442, 0},  {425, 0},  {408, 0},  {392, 0},  {375, 0},  {358, 0},  {342, 0},
    {325, 0},  {308, 0},  {292, 0},  {275, 0},  {258, 0},  {242, 0},  {225, 0},  {208, 0},
    {192, 0},  {175, 0},  {158, 0},  {142, 0},  {125, 0},  {108, 0},  {92, 0},   {75, 0},
    {58, 0},   {42, 0},   {25, 0},   {8, 0},    {-8, 0},   {-25, 0},  {-42, 0},  {-58, 0},
    {-75, 0},  {-92, 0},  {-108, 0}, {-125, 0}, {-142, 0}, {-158, 0}, {-175, 0}, {-192, 0},
    {-208, 0}, {-225, 0}, {-242, 0}, {-258, 0}, {-275, 0}, {-292, 0}, {-308, 0}, {-325, 0},
    {-342, 0}, {-358, 0}, {-375, 0}, {-392, 0}, {-408, 0}, {-425, 0}, {-442, 0}, {-458, 0}};

extern const ModelConfig CONFIG = {
    .id = ModelId::SINGLE_STRIP_TEST_DEVICE,
    .name = "Single Strip Test Rig",

    .num_strips = NUM_STRIPS,
    .strip_lengths = STRIP_LENGTHS,
    .pin_map = PIN_MAP,

    .screen_height_mm = 10,
    .screen_width_mm = 934,
    .cartesian_data = (const CartesianCoordinates*)STRIP_CARTESIAN,

    // This rig has no PSU: the strip is soldered straight onto the ESP32-C3's 3.3V
    // pin, fed by whatever the board's onboard LDO (typically an AP2112K-3.3, rated
    // 600mA) can supply off a laptop USB port - not the 5V every shipped model's
    // strip runs from, so this is the one model that overrides rail_millivolts.
    .rail_millivolts = 3300,

    // 250mA (a conservative guess derived from the LDO's datasheet rating) turned out
    // to be way under what this rig can actually sustain - hands-on testing at full
    // white with the corrected rail_millivolts in place showed no flicker/brownout
    // well above that. Raised to 1250mA based on that observed headroom, not a
    // measured PSU spec like #159 - this is still a lash-up test rig.
    .max_milliamps = 1250,
};

}  // namespace SingleStripTestDevice