#include <FastLED.h>

#include "geometry/geometry.h"
#include "geometry/model_config.h"

namespace L10_MK0
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
    .id = ModelId::L10_MK0,
    .name = "L10 MK0",

    .num_strips = NUM_STRIPS,
    .strip_lengths = STRIP_LENGTHS,
    .pin_map = PIN_MAP,

    .screen_height_mm = 84,
    .screen_width_mm = 84,
    .cartesian_data = (const CartesianCoordinates*)coords_L10,

    .min_frame_duration_ms = 7,  // 7ms = ~142fps

    // 1200mAh Li-Ion 18650, continuous discharge rated 0.6-1.8A, stepped up to the
    // 5V LED rail through a boost converter. Derived at the worst-case operating
    // point rather than the nominal one (#216 - the earlier 1130mA budget used the
    // cell's 3.7V nominal and browned out on a partly-discharged battery):
    //
    //   cell:    1.8A * 3.2V = 5.8W    3.2V, not 3.7V - the boost is a constant-power
    //                                  load, so the binding case is a low-SoC cell
    //                                  (~3.4V open-circuit) sagging ~0.2V under 1.8A
    //                                  across ~110mOhm of DC internal resistance.
    //   - MCU:   5.8W - 0.6W = 5.2W    the ESP32 runs off the same cell and is not in
    //                                  FastLED's estimate (its gMCU_mW reserves 25mA
    //                                  @5V and wrongly scales even that by brightness).
    //                                  An ESP32-C3 draws ~84mA @3.3V connected-idle and
    //                                  bursts to 335mA on WiFi TX - those bursts are
    //                                  what actually trips the brownout detector.
    //   * boost: 5.2W * 0.80 = 4.2W    80%, not 85%, for a 3.2V->5V step-up near the
    //                                  module's full load.
    //   / 5V:    4.2W / 5V  =  830mA
    //   * 0.8:                 650mA   FastLED's own model is documented approximate
    //                                  (~10%) and is a per-frame average; the brownout
    //                                  detector trips on microsecond dips.
    .max_milliamps = 650,

    // The 650mA budget above is calibrated against full white, the true
    // worst case - but the effects that actually run on the L10 are mostly
    // sparse, so a slider calibrated against that worst case leaves the top
    // ~45% of its travel doing nothing once FastLED's limiter engages (#237).
    // 128 trades away some of that safety margin for a slider whose whole
    // range is usable on typical content; FastLED's limiter still catches
    // any frame brighter than this reference.
    .brightness_reference_level = 128,
};

}  // namespace L10_MK0