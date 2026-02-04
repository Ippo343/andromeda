#pragma once

#include <Arduino.h>

// Forward declarations
struct CartesianCoordinates;
struct PolarCoordinates;
class LedStrip;

// Unique identifier for each model
enum class ModelId : uint8_t {
    ANDROMEDA_MK1 = 0,
    ANDROMEDA_MK2 = 1,
    L10 = 2,
    L25 = 3,
    L70 = 4,
    H10 = 5,
    // Add more models as needed
    // ...
    UNKNOWN = 255
};

// Configuration for a specific LED model
// All data stored in PROGMEM to save RAM
struct ModelConfig {
    ModelId id;
    const char* family;           // e.g., "Andromeda", "L-Series", "H-Series"
    const char* model_name;       // e.g., "Mk1", "Mk2", "L10"

    uint8_t num_strips;           // Number of LED strips in this model
    uint8_t max_leds_per_strip;   // Maximum LEDs in any strip (for buffer sizing)
    uint16_t screen_size_mm;      // Bounding box size

    // Per-strip data (arrays of length num_strips)
    const uint8_t* strip_lengths; // Actual number of LEDs in each strip
    const uint8_t* pin_map;       // GPIO pin for each strip

    // Coordinate data stored as flat arrays
    // Access as: cartesian_data[strip_idx * max_leds_per_strip + led_idx]
    const CartesianCoordinates* cartesian_data;
    const PolarCoordinates* polar_data;

    // Function pointer for model-specific FastLED initialization
    // Each model implements this to call FastLED.addLeds with correct template parameters
    void (*initialize_fastled)(LedStrip* strips);
};
