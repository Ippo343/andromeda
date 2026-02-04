#pragma once

#include <ArduinoLog.h>
#include <FastLED.h>
#include <Preferences.h>
#include <math.h>
#include "utils.h"
#include "model_config.h"
#include "model_registry.h"

// Full circle in centi-degrees
// TODO: is centidegrees still a good choice?
// The ESP32 has an FPU, but maybe even the accum types from FastLED would be better?
const unsigned short FULL_CIRCLE = 360 * 100;

// Cartesian coordinates (x, y) in millimeters
struct CartesianCoordinates {
    short x;
    short y;
};

// Polar coordinates: radius in mm, angle in centi-degrees
struct PolarCoordinates {
    unsigned short radius;
    unsigned short cdegrees;
};

// Represents geometric information for a single LED
class Led {
public:
    uint8_t idx;                          // Index in the strip that contains it
    CartesianCoordinates fixedCartesian;  // Physical location, never changes
    PolarCoordinates fixedPolar;          // Physical location (polar), never changes
    CartesianCoordinates cartesian;       // Transformed coordinates (for effects)
    PolarCoordinates polar;               // Transformed polar coordinates (for effects)
};

// Data related to a single strip
// Contains geometry info and color buffer for rendering
class LedStrip {
public:
    uint8_t idx;           // Index of the strip in the structure
    uint8_t num_leds;      // Actual number of LEDs in this strip
    Led* leds;             // Dynamically allocated array of LEDs
    CRGB* buffer;          // Color buffer for FastLED rendering

    LedStrip();
    ~LedStrip();

    // Allocate memory for LEDs and buffer
    void allocate(uint8_t count);

    // Free allocated memory
    void deallocate();
};

// Main geometry manager
// Handles model selection, initialization, and coordinate transforms
class Geometry {
private:
    const ModelConfig* config;
    LedStrip* strips;
    bool initialized;

    // Load model coordinates from PROGMEM into RAM
    void loadCoordinates();

public:
    Geometry();
    ~Geometry();

    // Initialize with a specific model
    // Should be called once at startup
    void initialize(ModelId model_id);

    // Get the current model configuration
    inline const ModelConfig* getConfig() const { return config; }

    // Get number of strips in current model
    inline uint8_t getNumStrips() const { return config ? config->num_strips : 0; }

    // Get a specific strip (bounds checking in debug builds)
    inline LedStrip& getStrip(uint8_t i) {
        return strips[i];
    }

    // Get screen size
    inline unsigned short getScreenSize() const {
        return config ? config->screen_size_mm : 0;
    }

    inline unsigned short getScreenHalfSize() const {
        return getScreenSize() / 2;
    }

    // Apply random rotation transform to all LED coordinates
    void applyGlobalRandomRotation();

    // Reset all transforms (restore original coordinates)
    void resetGlobalTransform();

    // Check if geometry is initialized
    inline bool isInitialized() const { return initialized; }
};

// Global geometry instance
extern Geometry GEOMETRY;

// Convenience macros for iterating over strips and LEDs
// These now use the dynamic geometry
#define FOR_EACH_STRIP for (uint8_t iStrip = 0; iStrip < GEOMETRY.getNumStrips(); iStrip++)
#define FOR_EACH_LED for (uint8_t iLed = 0; iLed < GEOMETRY.getStrip(iStrip).num_leds; iLed++)

// Factory configuration functions
namespace FactoryConfig {
    // Set the model ID in persistent storage (NVS)
    // Should only be called during factory configuration
    void setModelId(ModelId model_id);

    // Get the configured model ID from persistent storage
    // Returns ModelId::UNKNOWN if not configured
    ModelId getModelId();

    // Check if device has been configured with a model
    bool isConfigured();
}