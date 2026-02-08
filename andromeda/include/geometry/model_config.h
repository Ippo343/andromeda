#pragma once

#include <Arduino.h>

// Forward declarations
struct CartesianCoordinates;
struct PolarCoordinates;
class LedStrip;

enum class FamilyID : uint8_t {
    UNKNOWN = 0,        // For error handling purposes
    TEST_DEVICES,
    ANDROMEDA,
    L_SERIES,
};

// Unique identifier for each model
enum class ModelId : uint16_t {

    UNKNOWN = 0,    // For error handling purposes

    // That ridiculous little thing that I brought to Eindhoven to replicate an L10
    SINGLE_STRIP_TEST_DEVICE = ((uint8_t)FamilyID::TEST_DEVICES << 8) | 0,

    // The first prototype Andromeda model that started this whole madness
    ANDROMEDA_MK1 = ((uint8_t)FamilyID::ANDROMEDA << 8) | 0,

    // The actually commercially viable products
    L10 = ((uint8_t)FamilyID::L_SERIES << 8) | 0,
    L25 = ((uint8_t)FamilyID::L_SERIES << 8) | 1,
    L70 = ((uint8_t)FamilyID::L_SERIES << 8) | 2,
};


// Configuration for a specific LED model
struct ModelConfig {
    ModelId id;
    const char* name;             // e.g., "Andromeda", "L-Series", "H-Series"

    uint8_t num_strips;           // Number of LED strips in this model
    const uint8_t* strip_lengths; // Actual number of LEDs in each strip
    const uint8_t* pin_map;       // GPIO pin for each strip

    uint16_t screen_size_mm;      // Bounding box size

    // Coordinate data stored as flat arrays
    // Access as: cartesian_data[strip_idx * max_leds_per_strip + led_idx]
    const CartesianCoordinates* cartesian_data;
    const PolarCoordinates* polar_data;
};
