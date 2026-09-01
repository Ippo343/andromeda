#pragma once

#include <Arduino.h>
#include <FastLED.h>

// Forward declarations
struct CartesianCoordinates;
struct PolarCoordinates;
class LedStrip;

enum class FamilyID : uint8_t
{
    UNKNOWN = 0,  // For error handling purposes
    TEST_DEVICES,
    ANDROMEDA,
    L_SERIES,
};

#define MODEL_ID(family, model) (((uint8_t)FamilyID::family << 8) | model)

// Unique identifier for each model
enum class ModelId : uint16_t
{
    UNKNOWN = 0,  // For error handling purposes

    // That ridiculous little thing that I brought to Eindhoven to replicate an L10
    SINGLE_STRIP_TEST_DEVICE = MODEL_ID(TEST_DEVICES, 0),

    // Fake square grid, simulator-only: gives the emitter-field effects
    // (BezierSwarm, MultiPendulum, RGBodyProblem) a plain 2D layout to visualize on,
    // instead of Andromeda's rings. The registry entry (model_registry.cpp) and the
    // ~6.4 KB coordinate table are compiled out of hardware builds, so on-device
    // getModelConfig(GRID_TEST_DEVICE) returns nullptr - the enumerator is kept only
    // so the id space stays stable.
    GRID_TEST_DEVICE = MODEL_ID(TEST_DEVICES, 1),

    // The first prototype Andromeda model that started this whole madness
    ANDROMEDA_MK1 = MODEL_ID(ANDROMEDA, 0),

    // The actually "commercially" "viable" products
    L70_MK1 = MODEL_ID(L_SERIES, 0),
    L10_MK1 = MODEL_ID(L_SERIES, 1),
    L10_MK2 = MODEL_ID(L_SERIES, 2),
};

// Configuration for a specific LED model
struct ModelConfig
{
    ModelId id;
    const char* name;  // e.g., "Andromeda", "L-Series", "H-Series"

    size_t num_strips;            // Number of LED strips in this model
    const size_t* strip_lengths;  // Actual number of LEDs in each strip
    const uint8_t* pin_map;       // GPIO pin for each strip

    uint16_t screen_height_mm;
    uint16_t screen_width_mm;

    // Coordinate data stored as flat arrays
    const CartesianCoordinates* cartesian_data;

    // Minimum frame duration in milliseconds (for fps capping)
    uint8_t min_frame_duration_ms = 0;

    // 5V rail budget in milliamps, fed to FastLED.setMaxPowerInVoltsAndMilliamps() at
    // boot (main.cpp). FastLED estimates the actual per-frame draw from the rendered
    // colors and globally dims to stay under this, on top of (not instead of) the
    // user-facing brightness slider - the safety net for a customer sliding to 255 and
    // picking white on a PSU that can't deliver it.
    //
    // TODO(#<placeholder-issue>): this is a conservative placeholder, not a measured
    // PSU rating - same budget for every model regardless of LED count. Replace with
    // the real number for each model's actual shipped PSU before shipping units.
    uint16_t max_milliamps = 2000;

    bool isInFamily(FamilyID family) const { return ((uint16_t)id >> 8) == ((uint16_t)family); }
};
