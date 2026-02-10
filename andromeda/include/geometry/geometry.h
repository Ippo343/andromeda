#pragma once

#include <ArduinoLog.h>
#include <FastLED.h>
#include <Preferences.h>
#include <math.h>

#include "model_config.h"
#include "model_registry.h"
#include "utils.h"

// Full circle in centi-degrees
// TODO: is centidegrees still a good choice?
// The ESP32 has an FPU, but maybe even the accum types from FastLED would be better?
const unsigned short FULL_CIRCLE = 360 * 100;

// Cartesian coordinates (x, y) in millimeters
struct CartesianCoordinates
{
    int16_t x;
    int16_t y;
};

// Polar coordinates: radius in mm, angle in centi-degrees
struct PolarCoordinates
{
    uint16_t radius;
    uint16_t cdegrees;

    PolarCoordinates() : radius(0), cdegrees(0) {}

    // Constructor from Cartesian coordinates
    PolarCoordinates(CartesianCoordinates cart)
    {
        // Calculate radius
        float x_sq = (float)cart.x * cart.x;
        float y_sq = (float)cart.y * cart.y;
        radius = (uint16_t)sqrt(x_sq + y_sq);

        // Calculate angle in centi-degrees (0-36000 for 0-360 degrees)
        float angle_rad = atan2f((float)cart.y, (float)cart.x);
        float angle_deg = angle_rad * 18000.0f / PI;

        // Normalize to 0-36000 range
        if (angle_deg < 0) { angle_deg += 36000.0f; }

        cdegrees = (uint16_t)angle_deg;
    }
};

// Represents geometric information for a single LED
//
// TODO: I suspect the layout of this struct is the memory bottleneck.
// As it is each Led struct is 20 bytes, and a cache line should be 32 bytes, so we kinda suck.
// But effects never access the fixed coordinates, and the index can be passed as an argument to
// evaluate, and that would take us down to 8 bytes theoretically which means 4 Leds per cache line.
//
// All of this assumes we really are RAM bound, but there's another likely culprit which is the LED
// driver. WS2812B run at 800kHz so it's not quite clear if we are bound to the cache miss or to the
// LED driver speed.
class Led
{
   public:
    size_t idx;                           // Index in the strip that contains it
    CartesianCoordinates fixedCartesian;  // Physical location, never changes
    PolarCoordinates fixedPolar;          // Physical location (polar), never changes
    CartesianCoordinates cartesian;       // Transformed coordinates (for effects)
    PolarCoordinates polar;               // Transformed polar coordinates (for effects)
};

// Data related to a single strip
// Contains geometry info and color buffer for rendering
class LedStrip
{
   public:
    size_t idx;       // Index of the strip in the structure
    size_t num_leds;  // Actual number of LEDs in this strip
    Led* leds;        // Dynamically allocated array of LEDs
    CRGB* buffer;     // Color buffer for FastLED rendering

    LedStrip();
    ~LedStrip();

    // Allocate memory for LEDs and buffer
    void allocate(size_t count);

    // Free allocated memory
    void deallocate();
};

// Main geometry manager
// Handles model selection, initialization, and coordinate transforms
class Geometry
{
   private:
    const ModelConfig* config;
    LedStrip* strips;

    // Load model coordinates from PROGMEM into RAM
    void loadCoordinates();

   public:
    Geometry();
    ~Geometry();

    // Initialize with a specific model
    // Should be called once at startup
    void initialize(ModelId model_id);

    inline const LedStrip* getStrips() const { return strips; }

    // Get the current model configuration
    inline const ModelConfig* getConfig() const { return config; }

    // Get number of strips in current model
    inline size_t getNumStrips() const { return config ? config->num_strips : 0; }

    // Get a specific strip (bounds checking in debug builds)
    inline LedStrip& getStrip(size_t i) { return strips[i]; }

    // Get screen size
    inline unsigned short getScreenHeight() const { return config->screen_height_mm; }
    inline unsigned short getScreenWidth() const { return config->screen_width_mm; }
    inline unsigned short getScreenHalfHeight() const { return getScreenHeight() / 2; }
    inline unsigned short getScreenHalfWidth() const { return getScreenWidth() / 2; }

    inline unsigned short getScreenRadius() const {
        static unsigned short screenRadius = 0;
        if (screenRadius == 0) {
            screenRadius = max(getScreenHalfHeight(), getScreenHalfWidth());
        }
        return screenRadius;
    }

    // Apply random rotation transform to all LED coordinates
    void applyGlobalRandomRotation();

    // Reset all transforms (restore original coordinates)
    void resetGlobalTransform();
};

// Global geometry instance
extern Geometry GEOMETRY;

// Convenience macros for iterating over strips and LEDs
// These now use the dynamic geometry
#define FOR_EACH_STRIP for (size_t iStrip = 0; iStrip < GEOMETRY.getNumStrips(); iStrip++)
#define FOR_EACH_LED(iStrip) \
    for (size_t iLed = 0; iLed < GEOMETRY.getStrip(iStrip).num_leds; iLed++)

// Factory configuration functions
namespace FactoryConfig
{
// Set the model ID in persistent storage (NVS)
// Should only be called during factory configuration
void setModelId(ModelId model_id);

// Get the configured model ID from persistent storage
// Returns ModelId::UNKNOWN if not configured
ModelId getModelId();

// Check if device has been configured with a model
bool isConfigured();
}  // namespace FactoryConfig