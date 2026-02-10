#pragma once

// The adopter module for general utilities,
// like typedefs that I like and small functions reused everywhere

#include <Arduino.h>
#include <FastLED.h>
#include <stdint.h>

#define milliseconds_t unsigned long

#define SECONDS *1000
#define MINUTES *60 SECONDS

// Looping Index macro: keeps the index always within the strip
// simplifying code that needs to access consecutive LEDs.
// Basically python's array[-1] but for C
#define LI(idx) ((idx + GEOMETRY.getStrip(idx).num_leds) % GEOMETRY.getStrip(idx).num_leds)

void seedRNGs();

long scaledCubicWave8(milliseconds_t t, milliseconds_t period, long minV, long maxV);
float Q_rsqrt(float number);
long cmap(long x, long in_low, long in_high, long out_low, long out_high);
float slowSin(unsigned long ms, float bpm, uint8_t minVal, uint8_t maxVal);
void shuffle(int* array, int size);  // Fisher-Yates shuffle

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
    virtual void randomize() { value = random(min, max + 1); }
};

class RandBool : public RandParam<bool, 0, 1>
{
   public:
    void randomize() override;
};

// Specialized random parameter that can only be -1 or 1, but not 0.
// This is useful as a randomly chosen sign for math operations, e.g:
//    (-1|1) * (led.x)
class RandSign : public RandParam<char, -1, 1>
{
   public:
    void randomize() override;
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
