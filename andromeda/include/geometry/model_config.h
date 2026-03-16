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

    // The first prototype Andromeda model that started this whole madness
    ANDROMEDA_MK1 = MODEL_ID(ANDROMEDA, 0),

    // The actually "commercially" "viable" products
    L70_MK1 = MODEL_ID(L_SERIES, 0),
    L10_MK1 = MODEL_ID(L_SERIES, 1),
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

    // Maximum CPU frequency for this model
    // Predefined as the actual CPU's default, but it can be overwritten per model.
    // The main example of this is the L10, which still runs at 400fps even at 80MHz.
    uint8_t max_cpu_freq_mhz = F_CPU_MHZ;

    bool isInFamily(FamilyID family) const { return ((uint16_t)id >> 8) == ((uint16_t)family); }
};
