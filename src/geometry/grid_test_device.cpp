#include <FastLED.h>

#include <array>
#include <utility>

#include "geometry/geometry.h"
#include "geometry/model_config.h"

namespace GridTestDevice
{

// Fake square grid, wired as a single strip in serpentine (boustrophedon) order -
// alternating scan direction each row, like a real LED matrix panel. It has no
// real-world counterpart; its only job is giving the emitter-field effects a
// plain 2D layout to tune against in the simulator, instead of Andromeda's rings.
//
// Simulator/native-tests only: this file is filtered out of the firmware builds
// (platformio.ini) and model_registry.cpp drops the registry entry under
// !NATIVE_BUILD, so the ~6.4 KB compile-time GRID_CARTESIAN table never reaches
// hardware.

constexpr size_t GRID_SIZE = 40;
constexpr size_t NUM_LEDS = GRID_SIZE * GRID_SIZE;
constexpr int16_t SPACING_MM = 20;
constexpr int16_t HALF_SPAN_MM = (int16_t)((GRID_SIZE - 1) * SPACING_MM / 2);

constexpr CartesianCoordinates gridPoint(size_t idx)
{
    size_t row = idx / GRID_SIZE;
    size_t rawCol = idx % GRID_SIZE;
    size_t col = (row % 2 == 0) ? rawCol : (GRID_SIZE - 1 - rawCol);
    return {(int16_t)((int16_t)(col * SPACING_MM) - HALF_SPAN_MM),
            (int16_t)((int16_t)(row * SPACING_MM) - HALF_SPAN_MM)};
}

template <size_t... I>
constexpr std::array<CartesianCoordinates, NUM_LEDS> makeGrid(std::index_sequence<I...>)
{
    return {{gridPoint(I)...}};
}

// Computed at compile time rather than hand-listed like the other models' literal
// arrays - 1600 points isn't practical to hand-specify, and unlike a real strip this
// one has no physical layout to transcribe. Not PROGMEM-tagged: on ESP32 the
// pgm_read_ptr/memcpy_P calls that consume cartesian_data (geometry.cpp) are plain
// reads regardless (flash-mapped rodata is directly addressable there), so a
// constexpr std::array works identically to the other models' PROGMEM C arrays.
constexpr std::array<CartesianCoordinates, NUM_LEDS> GRID_CARTESIAN =
    makeGrid(std::make_index_sequence<NUM_LEDS>{});

const uint8_t NUM_STRIPS = 1;
const PROGMEM size_t STRIP_LENGTHS[NUM_STRIPS] = {NUM_LEDS};
// Placeholder pin, never actually wired up (bindHardwareDrivers() is skipped in the
// native/simulator path this model is meant for) - pin 1 is valid on every board
// variant, same as SingleStripTestDevice's.
const PROGMEM uint8_t PIN_MAP[NUM_STRIPS] = {1};

extern const ModelConfig CONFIG = {
    .id = ModelId::GRID_TEST_DEVICE,
    .name = "Grid Test Rig",

    .num_strips = NUM_STRIPS,
    .strip_lengths = STRIP_LENGTHS,
    .pin_map = PIN_MAP,

    .screen_height_mm = (uint16_t)(GRID_SIZE * SPACING_MM),
    .screen_width_mm = (uint16_t)(GRID_SIZE * SPACING_MM),
    .cartesian_data = GRID_CARTESIAN.data(),

    .preferred_cpu_freq_mhz = 80,
};

}  // namespace GridTestDevice
