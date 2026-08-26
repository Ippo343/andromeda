#include "geometry/geometry.h"

// Global geometry instance
Geometry GEOMETRY;

// ============================================================================
// LedStrip Implementation
// ============================================================================

LedStrip::LedStrip() : idx(0), num_leds(0), leds(nullptr), buffer(nullptr) {}

LedStrip::~LedStrip() { deallocate(); }

void LedStrip::allocate(size_t count, bool allocate_color_buffer)
{
    deallocate();

    num_leds = count;
    leds = new Led[count];

    // The color buffer is only required for the strips used for rendering, not the fixed coordinate
    // backup strips
    if (allocate_color_buffer)
    {
        buffer = new CRGB[count];
        for (size_t i = 0; i < count; i++) { buffer[i] = CRGB::Black; }
        Log.verboseln("Strip %d: allocated %d LEDs", idx, count);
    }
    else
    {
        buffer = nullptr;
        Log.verboseln("Strip %d: allocated %d LEDs (no buffer)", idx, count);
    }
}

void LedStrip::deallocate()
{
    if (leds)
    {
        delete[] leds;
        leds = nullptr;
    }
    if (buffer)
    {
        delete[] buffer;
        buffer = nullptr;
    }
    num_leds = 0;
}

// ============================================================================
// Geometry Implementation
// ============================================================================

void addLedsToPin(uint8_t pin, CRGB* buffer, int count)
{
    switch (pin)
    {
        // PINS VALID ON ALL MODELS (C3, S3, WROOM)
        case 1:
            FastLED.addLeds<WS2812B, 1, GRB>(buffer, count);
            break;
        case 2:
            FastLED.addLeds<WS2812B, 2, GRB>(buffer, count);
            break;
        case 4:
            FastLED.addLeds<WS2812B, 4, GRB>(buffer, count);
            break;
        case 5:
            FastLED.addLeds<WS2812B, 5, GRB>(buffer, count);
            break;
        case 10:
            Log.errorln("Pin 10 is forbidden by FastLED");
            break;

// PINS VALID ONLY ON S3 and WROOM (High GPIO numbers)
#if !defined(ESP32_C3)
        case 12:
            FastLED.addLeds<WS2812B, 12, GRB>(buffer, count);
            break;
        case 13:
            FastLED.addLeds<WS2812B, 13, GRB>(buffer, count);
            break;
        case 14:
            FastLED.addLeds<WS2812B, 14, GRB>(buffer, count);
            break;
        case 15:
            FastLED.addLeds<WS2812B, 15, GRB>(buffer, count);
            break;
        case 16:
            FastLED.addLeds<WS2812B, 16, GRB>(buffer, count);
            break;
        case 17:
            FastLED.addLeds<WS2812B, 17, GRB>(buffer, count);
            break;
        case 18:
            FastLED.addLeds<WS2812B, 18, GRB>(buffer, count);
            break;
        case 19:
            FastLED.addLeds<WS2812B, 19, GRB>(buffer, count);
            break;
        case 21:
            FastLED.addLeds<WS2812B, 21, GRB>(buffer, count);
            break;
#if defined(ESP32_S3)
        case 22:
            Log.errorln("Pin 22 is forbidden by FastLED on S3");
            break;
#else
        case 22:
            FastLED.addLeds<WS2812B, 22, GRB>(buffer, count);
            break;
#endif
        case 23:
            Log.errorln("Pin 23 is forbidden by FastLED");
            break;
        case 25:
            Log.errorln("Pin 25 is forbidden by FastLED");
            break;
        case 26:
            FastLED.addLeds<WS2812B, 26, GRB>(buffer, count);
            break;
        case 27:
            Log.errorln("Pin 27 is forbidden by FastLED");
            break;
#endif

// PINS VALID ONLY ON WROOM (Even higher GPIO numbers)
#if defined(ESP32_WROOM)
        case 32:
            FastLED.addLeds<WS2812B, 32, GRB>(buffer, count);
            break;
        case 33:
            FastLED.addLeds<WS2812B, 33, GRB>(buffer, count);
            break;
#endif

        default:
            Log.errorln("Pin %d is not valid for this specific hardware variant!", pin);
            break;
    }
}

Geometry::Geometry() : config(nullptr), _fixedStrips(nullptr), strips(nullptr) {}

Geometry::~Geometry()
{
    if (strips)
    {
        delete[] strips;
        strips = nullptr;
    }
    if (_fixedStrips)
    {
        delete[] _fixedStrips;
        _fixedStrips = nullptr;
    }
}

void Geometry::allocateAndLoadCoordinates(ModelId model_id)
{
    // Get model configuration
    config = getModelConfig(model_id);
    if (!config)
    {
        Log.errorln("Failed to find model configuration for ID %d", (uint8_t)model_id);
        // TODO: handle this more gracefully (e.g., fallback to a default model or enter a safe
        // mode)
    }

    Log.noticeln("Initializing geometry for: %s", config->name);

    // Free any previously allocated strips before re-initializing
    if (strips) delete[] strips;
    if (_fixedStrips) delete[] _fixedStrips;

    // Allocate strip array
    strips = new LedStrip[config->num_strips];
    _fixedStrips = new LedStrip[config->num_strips];

    // Initialize each strip
    for (size_t i = 0; i < config->num_strips; i++)
    {
        strips[i].idx = i;
        _fixedStrips[i].idx = i;

        // pgm_read_byte would silently truncate any strip >= 256 LEDs (only ever
        // exposed once a model needed one - all real strips happen to stay under
        // that). memcpy_P reads the full size_t width instead, same idiom already
        // used for cartesian_data below.
        size_t strip_length;
        memcpy_P(&strip_length, &config->strip_lengths[i], sizeof(size_t));

        strips[i].allocate(strip_length, true);
        _fixedStrips[i].allocate(strip_length, false);
    }

    // Load coordinates from PROGMEM
    loadCoordinates();
}

void Geometry::bindHardwareDrivers()
{
    // Initialize FastLED controllers for each strip
    for (size_t i = 0; i < config->num_strips; i++)
    {
        uint8_t pin = pgm_read_byte(&config->pin_map[i]);
        addLedsToPin(pin, strips[i].buffer, strips[i].num_leds);
    }
}

void Geometry::initialize(ModelId model_id)
{
    allocateAndLoadCoordinates(model_id);
    bindHardwareDrivers();
    Log.noticeln("Geometry initialized successfully");
}

void Geometry::initializeForTest(ModelId model_id) { allocateAndLoadCoordinates(model_id); }

void Geometry::loadCoordinates()
{
    Log.verboseln("Loading coordinates from PROGMEM...");

    uint16_t current_offset = 0;  // Tracks the current position in the flat PROGMEM array

    for (size_t iStrip = 0; iStrip < config->num_strips; iStrip++)
    {
        size_t strip_length = strips[iStrip].num_leds;

        for (size_t iLed = 0; iLed < strip_length; iLed++)
        {
            // The coordinate is located at (start of strip + current led index)
            uint16_t data_index = current_offset + iLed;

            // Copy Cartesian data from PROGMEM
            CartesianCoordinates cart;
            const CartesianCoordinates* progmem_ptr =
                (const CartesianCoordinates*)pgm_read_ptr(&config->cartesian_data);
            memcpy_P(&cart, &progmem_ptr[data_index], sizeof(CartesianCoordinates));

            _fixedStrips[iStrip].leds[iLed].cartesian = cart;
            strips[iStrip].leds[iLed].cartesian = cart;

            _fixedStrips[iStrip].leds[iLed].polar = PolarCoordinates(cart);
            Log.verboseln("Strip %d, LED %d: Cartesian(%d, %d), Polar(%d, %d)", iStrip, iLed,
                          cart.x, cart.y, _fixedStrips[iStrip].leds[iLed].polar.radius,
                          _fixedStrips[iStrip].leds[iLed].polar.cdegrees);
            strips[iStrip].leds[iLed].polar = _fixedStrips[iStrip].leds[iLed].polar;
        }

        // After finishing a strip, move the offset forward by the length of that strip
        current_offset += strip_length;
    }

    Log.verboseln("Coordinates loaded successfully");
}

void Geometry::applyGlobalRandomRotation()
{
    // Pick a random angle for the rotation
    float theta = (random(0, 1000) / 1000.0) * 2 * PI;

    // Same angle, in centidegrees (to transform the polar coordinates)
    int tcDeg = (int)(theta * (100 * 180.0 / PI));

    Log.noticeln("Applying global rotation: %d degrees", (tcDeg / 100));

    float cosT = cos(theta);
    float sinT = sin(theta);

    FOR_EACH_STRIP
    {
        FOR_EACH_LED(iStrip)
        {
            // Real physical coordinates of the LED
            CartesianCoordinates r = _fixedStrips[iStrip].leds[iLed].cartesian;

            // Apply inverse rotation matrix
            strips[iStrip].leds[iLed].cartesian.x = (short)(r.x * cosT + r.y * sinT);
            strips[iStrip].leds[iLed].cartesian.y = (short)(-r.x * sinT + r.y * cosT);

            // Update polar angle
            strips[iStrip].leds[iLed].polar.cdegrees =
                ((int)_fixedStrips[iStrip].leds[iLed].polar.cdegrees - tcDeg) % FULL_CIRCLE;
        }
    }
}

void Geometry::resetGlobalTransform()
{
    Log.noticeln("Resetting global transform");

    FOR_EACH_STRIP
    {
        FOR_EACH_LED(iStrip)
        {
            strips[iStrip].leds[iLed].cartesian = _fixedStrips[iStrip].leds[iLed].cartesian;
            strips[iStrip].leds[iLed].polar = _fixedStrips[iStrip].leds[iLed].polar;
        }
    }
}

// ============================================================================
// Factory Configuration
// ============================================================================

namespace FactoryConfig
{
const char* PREFS_NAMESPACE = "device";
const char* MODEL_ID_KEY = "model_id";

void setModelId(ModelId model_id)
{
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUShort(MODEL_ID_KEY, (uint16_t)model_id);
    prefs.end();

    Log.noticeln("Factory config: Set model ID to %d (%s)", (uint16_t)model_id,
                 getModelName(model_id));
}

ModelId getModelId()
{
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);  // read-only
    uint16_t id = prefs.getUShort(MODEL_ID_KEY, (uint16_t)ModelId::UNKNOWN);
    prefs.end();

    return (ModelId)id;
}

bool isConfigured() { return getModelId() != ModelId::UNKNOWN; }
}  // namespace FactoryConfig
