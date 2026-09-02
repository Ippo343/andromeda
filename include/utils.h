#pragma once

// The adopter module for general utilities,
// like typedefs that I like and small functions reused everywhere

#include <Arduino.h>
#include <FastLED.h>
#include <stdint.h>

#define milliseconds_t unsigned long

#define SECONDS *1000
#define MINUTES *60 SECONDS

// FreeRTOS's pdMS_TO_TICKS(x) expands to ((x) * configTICK_RATE_HZ) / 1000
// evaluated in 32-bit TickType_t, so the intermediate product overflows for
// large delays even when the resulting tick count fits: pdMS_TO_TICKS(24h)
// computes 86_400_000 * 1000 mod 2^32 / 1000 = ~500654 ticks (~8m21s) instead
// of 86_400_000. Use this for any vTaskDelay where ms * configTICK_RATE_HZ can
// exceed 2^32 (ms above ~4.29e6). The firmware runs a 1 kHz FreeRTOS tick
// (CONFIG_FREERTOS_HZ=1000, static_assert'd at the call site), so one tick is
// one millisecond - the whole job here is doing it at a width that doesn't
// wrap.
constexpr uint32_t ticksFromMs(uint64_t ms) { return static_cast<uint32_t>(ms); }

// This is a helper to allow negative indexing in vectors, like in Python.
template <typename T>
inline auto& py_get(T& container, int index)
{
    int size = static_cast<int>(container.size());
    int idx = (index % size + size) % size;
    return container[idx];
}

void seedRNGs();

long scaledCubicWave8(milliseconds_t t, milliseconds_t period, long minV, long maxV);
float Q_rsqrt(float number);
long cmap(long x, long in_low, long in_high, long out_low, long out_high);
float slowSin(unsigned long ms, float bpm, uint8_t minVal, uint8_t maxVal);
void shuffle(int* array, int size);  // Fisher-Yates shuffle

// Converts a per-second event rate into a probability threshold for a single Bernoulli
// roll of random(rollLimit) this frame (i.e. an event fires when random(rollLimit) < the
// returned threshold), given the elapsed time dt (ms) since the last frame.
// Frame-rate independent: the expected number of events per second stays constant
// regardless of how often this is called.
//
// True model is P(>=1 event in dt) = 1 - exp(-rate * dt/1000), but rate*dt/1000 is always
// tiny at realistic LED frame rates and per-LED spark rates (that's what makes "roll a
// dice every frame for every LED" viable at all), so this uses the first-order Taylor
// approximation 1 - exp(-x) =~ x, i.e. p =~ rate * dt/1000. Clamped to `rollLimit` as a
// safety bound for the degenerate case of a stalled/huge dt, where the approximation would
// break down - not worth branching to the exact exp() model for that: the clamp already
// makes it safe, and it isn't a case where visual precision matters.
uint32_t rateToThreshold(float ratePerSecond, milliseconds_t dt, uint32_t rollLimit);

// Single-event version of the same roll: "did an event of rate ratePerSecond happen this
// frame". Wraps rateToThreshold() with a fixed roll limit precise enough for any realistic
// per-second rate, so callers that just want a one-shot Bernoulli trial (a pluck, a kick, a
// spawn) don't each need their own private *_ROLL_LIMIT constant and random()/threshold
// dance.
bool rollEvent(float ratePerSecond, milliseconds_t dt);

// Accumulates a per-second loss rate into a fade amount for fadeToBlackBy/scale8 (0-255),
// carrying the sub-quantum remainder forward in `debt` so the time-averaged decay rate is
// correct even when a single frame's loss is smaller than 1/255 - without this, a fast
// device would round every frame's loss down to zero and never decay at all.
// Call once per frame; `debt` must persist across calls for one decaying buffer (e.g. an
// effect member, or a `mutable` variable captured by an animation's segment lambda).
uint8_t accumulateFadeAmount(float& debt, float lossRatePerSecond, milliseconds_t dt);

// Paths for the log files (defined in utils.cpp)
// They need to be shared because the comms also need to know them
// so they can serve them over HTTP
extern const char* LOG_FILE_CUR;
extern const char* LOG_FILE_OLD;

// This picks a random T value when instantiated between min and max (inclusive)
template <typename T, T min, T max>
class RandParam
{
   protected:
    T value;

   public:
    RandParam() { randomize(); }  // including the max
    inline operator T() const { return value; }
    void randomize() { value = random(min, max + 1); }
};

// RandBool/RandSign are NOT overriding randomize() below - RandParam::randomize() isn't
// virtual (there's no need for every value-typed RandParam<T,min,max> instance to carry a
// vtable pointer/virtual destructor just for these two subclasses). Their randomize()
// merely hides the base one by name, which is exactly what's wanted here: it's resolved
// lexically at the call site, not by runtime type, so RandParam's own constructor (which
// unqualified-calls randomize()) always calls RandParam::randomize() - producing an
// initial value from the base's [min,max] algorithm, which for RandSign's [-1,1] range can
// land on 0. Each subclass's own constructor then re-calls randomize() unqualified from
// its own scope, which resolves to the hiding (derived) overload instead.
class RandBool : public RandParam<bool, 0, 1>
{
   public:
    RandBool() { randomize(); }
    void randomize();
};

// Specialized random parameter that can only be -1 or 1, but not 0.
// This is useful as a randomly chosen sign for math operations, e.g:
//    (-1|1) * (led.x)
class RandSign : public RandParam<char, -1, 1>
{
   public:
    // See RandBool's constructor comment: without this, the base
    // RandParam::randomize() runs instead, which can produce 0.
    RandSign() { randomize(); }
    void randomize();
};

// Represents a sine wave with randomly chosen bpm and direction
template <uint8_t minBpm, uint8_t maxBpm>
class RandSine
{
   protected:
    RandParam<uint8_t, minBpm, maxBpm> bpm;
    RandSign sign;

   public:
    RandSine() { randomize(); }

    void randomize()
    {
        bpm.randomize();
        sign.randomize();
    }

    uint8_t evaluate(long x) { return beatsin8(bpm, 0, 255, 0, sign * x); }
};
