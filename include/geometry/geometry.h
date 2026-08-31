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
        radius = (uint16_t)sqrtf(x_sq + y_sq);

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
// TODO: update: the cache lines were not the bottleneck, the LED driver is.
// But we can still optimize the memory layout for the cache, not because it's useful but because
// it's fun. As it is each Led struct is 20 bytes, and a cache line should be 32 bytes, so we kinda
// suck. But effects never access the fixed coordinates, and the index can be passed as an argument
// to evaluate, and that would take us down to 8 bytes theoretically which means 4 Leds per cache
// line.
class Led
{
   public:
    CartesianCoordinates cartesian;  // Transformed coordinates (for effects)
    PolarCoordinates polar;          // Transformed polar coordinates (for effects)
};

static_assert(sizeof(Led) == 8, "4 Leds should fit in a 32 byte cache line for optimal access");

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
    void allocate(size_t count, bool allocate_color_buffer);

    // Free allocated memory
    void deallocate();
};

// Main geometry manager
// Handles model selection, initialization, and coordinate transforms
class Geometry
{
   private:
    const ModelConfig* config;

    // A copy of the original strip data loaded from PROGMEM, used for resetting transforms.
    // This is done to improve cache locality during the effects calculations,
    // since the original coordinates are only used to apply and reset transforms.
    // These strips are a shadow copy without a color buffer
    LedStrip* _fixedStrips;
    LedStrip* strips;  // The actual strips used for math and rendering

    // Cached max(halfWidth, halfHeight), set once per initialize()/
    // initializeForTest() call. Used to be a function-local `static` inside
    // getScreenRadius(), which computed it once for the whole process and
    // never picked up a later re-initialize() with a different model - and
    // paid a static-init guard check on every call (hot: two effects read
    // it per LED per frame).
    unsigned short screenRadius = 0;

    // Load model coordinates from PROGMEM into RAM
    void loadCoordinates();

    // Pure part of initialize(): allocates strips and loads coordinate data.
    // No hardware/FastLED calls. Split out so it can run in native unit tests.
    void allocateAndLoadCoordinates(ModelId model_id);

    // Hardware-only part of initialize(): binds each strip's buffer to a
    // FastLED controller on its configured pin. Not testable host-side.
    void bindHardwareDrivers();

   public:
    Geometry();
    ~Geometry();

    // Initialize with a specific model
    // Should be called once at startup
    void initialize(ModelId model_id);

    // Same as initialize(), but skips bindHardwareDrivers() (the
    // FastLED.addLeds<...> calls), so it can run in native unit tests
    // without real/simulated LED hardware.
    void initializeForTest(ModelId model_id);

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

    inline unsigned short getScreenRadius() const { return screenRadius; }

    // Apply random rotation transform to all LED coordinates
    void applyGlobalRandomRotation();

    // Reset all transforms (restore original coordinates)
    void resetGlobalTransform();
};

// Global geometry instance
extern Geometry GEOMETRY;

// Convenience macros for iterating over strips and LEDs
// These now use the dynamic geometry
#define FOR_EACH_STRIP                                                    \
    for (size_t iStrip = 0, _forEachStripCount = GEOMETRY.getNumStrips(); \
         iStrip < _forEachStripCount; iStrip++)
// Reads num_leds once into the for-loop's own init-clause instead of
// re-evaluating GEOMETRY.getStrip(iStrip) on every iteration.
#define FOR_EACH_LED(iStrip)                                                     \
    for (size_t iLed = 0, _forEachLedCount = GEOMETRY.getStrip(iStrip).num_leds; \
         iLed < _forEachLedCount; iLed++)

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