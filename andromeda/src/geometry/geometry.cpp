#include "geometry/geometry.h"

// Global geometry instance
Geometry GEOMETRY;

// ============================================================================
// LedStrip Implementation
// ============================================================================

LedStrip::LedStrip() : idx(0), num_leds(0), leds(nullptr), buffer(nullptr) {
}

LedStrip::~LedStrip() {
    deallocate();
}

void LedStrip::allocate(uint8_t count) {
    // Free any existing allocation
    deallocate();

    num_leds = count;

    // Allocate LED geometry array
    leds = new Led[count];

    // Allocate color buffer for FastLED
    buffer = new CRGB[count];

    // Initialize LEDs
    for (uint8_t i = 0; i < count; i++) {
        leds[i].idx = i;
        buffer[i] = CRGB::Black;
    }

    Log.verboseln("Strip %d: allocated %d LEDs", idx, count);
}

void LedStrip::deallocate() {
    if (leds) {
        delete[] leds;
        leds = nullptr;
    }
    if (buffer) {
        delete[] buffer;
        buffer = nullptr;
    }
    num_leds = 0;
}

// ============================================================================
// Geometry Implementation
// ============================================================================

Geometry::Geometry() : config(nullptr), strips(nullptr), initialized(false) {
}

Geometry::~Geometry() {
    if (strips) {
        delete[] strips;
        strips = nullptr;
    }
}

void Geometry::initialize(ModelId model_id) {
    if (initialized) {
        Log.warningln("Geometry already initialized, reinitializing...");
        if (strips) {
            delete[] strips;
            strips = nullptr;
        }
    }

    // Get model configuration
    config = getModelConfig(model_id);
    if (!config) {
        Log.errorln("Failed to find model configuration for ID %d", (uint8_t)model_id);
        return;
    }

    Log.noticeln("Initializing geometry for: %s %s", config->family, config->model_name);
    Log.noticeln("  Strips: %d, Max LEDs/strip: %d, Screen: %d mm",
                 config->num_strips, config->max_leds_per_strip, config->screen_size_mm);

    // Allocate strip array
    strips = new LedStrip[config->num_strips];

    // Initialize each strip
    for (uint8_t i = 0; i < config->num_strips; i++) {
        strips[i].idx = i;

        // Read strip length from PROGMEM
        uint8_t strip_length = pgm_read_byte(&config->strip_lengths[i]);
        strips[i].allocate(strip_length);
    }

    // Load coordinates from PROGMEM
    loadCoordinates();

    // Initialize FastLED using model-specific function
    config->initialize_fastled(strips);

    initialized = true;
    Log.noticeln("Geometry initialized successfully");
}

void Geometry::loadCoordinates() {
    if (!config) return;

    Log.verboseln("Loading coordinates from PROGMEM...");

    for (uint8_t iStrip = 0; iStrip < config->num_strips; iStrip++) {
        uint8_t strip_length = strips[iStrip].num_leds;

        for (uint8_t iLed = 0; iLed < strip_length; iLed++) {
            // Calculate index into flat coordinate arrays
            // Arrays are organized as [strip][led] but stored flat
            uint16_t coord_index = (uint16_t)iStrip * config->max_leds_per_strip + iLed;

            // Read cartesian coordinates from PROGMEM
            CartesianCoordinates cart;
            memcpy_P(&cart, &config->cartesian_data[coord_index], sizeof(CartesianCoordinates));
            strips[iStrip].leds[iLed].fixedCartesian = cart;
            strips[iStrip].leds[iLed].cartesian = cart;

            // Read polar coordinates from PROGMEM
            PolarCoordinates polar;
            memcpy_P(&polar, &config->polar_data[coord_index], sizeof(PolarCoordinates));
            strips[iStrip].leds[iLed].fixedPolar = polar;
            strips[iStrip].leds[iLed].polar = polar;
        }
    }

    Log.verboseln("Coordinates loaded");
}

void Geometry::applyGlobalRandomRotation() {
    if (!initialized) {
        Log.errorln("Cannot apply rotation: geometry not initialized");
        return;
    }

    // Pick a random angle for the rotation
    float theta = (random(0, 1000) / 1000.0) * 2 * PI;

    // Same angle, in centidegrees (to transform the polar coordinates)
    int tcDeg = (int)(theta * (100 * 180.0 / PI));

    Log.noticeln("Applying global rotation: %d degrees", (tcDeg / 100));

    float cosT = cos(theta);
    float sinT = sin(theta);

    FOR_EACH_STRIP {
        FOR_EACH_LED {
            // Real physical coordinates of the LED
            CartesianCoordinates r = strips[iStrip].leds[iLed].fixedCartesian;

            // Apply inverse rotation matrix
            // This transforms the coordinate system, not the LEDs themselves
            // See original code comments for detailed explanation
            strips[iStrip].leds[iLed].cartesian.x = (short)(  r.x * cosT + r.y * sinT);
            strips[iStrip].leds[iLed].cartesian.y = (short)(- r.x * sinT + r.y * cosT);

            // Update polar angle
            strips[iStrip].leds[iLed].polar.cdegrees =
                ((int)strips[iStrip].leds[iLed].fixedPolar.cdegrees - tcDeg) % FULL_CIRCLE;
        }
    }
}

void Geometry::resetGlobalTransform() {
    if (!initialized) {
        Log.errorln("Cannot reset transform: geometry not initialized");
        return;
    }

    Log.noticeln("Resetting global transform");

    FOR_EACH_STRIP {
        FOR_EACH_LED {
            strips[iStrip].leds[iLed].cartesian = strips[iStrip].leds[iLed].fixedCartesian;
            strips[iStrip].leds[iLed].polar = strips[iStrip].leds[iLed].fixedPolar;
        }
    }
}

// ============================================================================
// Factory Configuration
// ============================================================================

namespace FactoryConfig {
    const char* PREFS_NAMESPACE = "device";
    const char* MODEL_ID_KEY = "model_id";

    void setModelId(ModelId model_id) {
        Preferences prefs;
        prefs.begin(PREFS_NAMESPACE, false);
        prefs.putUChar(MODEL_ID_KEY, (uint8_t)model_id);
        prefs.end();

        Log.noticeln("Factory config: Set model ID to %d (%s)",
                     (uint8_t)model_id, getModelName(model_id));
    }

    ModelId getModelId() {
        Preferences prefs;
        prefs.begin(PREFS_NAMESPACE, true);  // read-only
        uint8_t id = prefs.getUChar(MODEL_ID_KEY, (uint8_t)ModelId::UNKNOWN);
        prefs.end();

        return (ModelId)id;
    }

    bool isConfigured() {
        return getModelId() != ModelId::UNKNOWN;
    }
}
