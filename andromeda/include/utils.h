#ifndef ANDROMEDA_UTILS_H
#define ANDROMEDA_UTILS_H

// The adopter module for general utilities,
// like typedefs that I like and small functions reused everywhere

#include <stdint.h>   // uint8_t
#include <Arduino.h>

#define byte          uint8_t
#define milliseconds  unsigned long

#define SECONDS       * 1000
#define MINUTES       * 60 SECONDS

// I hate typing
#define FOR_EACH_STRIP  for (byte iStrip = 0; iStrip < NUM_STRIPS; iStrip++)
#define FOR_EACH_LED    for (byte iLed = 0; iLed < LEDS_PER_STRIP; iLed++)

// Looping Index macro: keeps the index always within the strip
// simplifying code that needs to access consecutive LEDs.
// Basically python's array[-1] but for C
#define LI(idx) ( (idx + LEDS_PER_STRIP) % LEDS_PER_STRIP )

void seedRNGs();

long scaledCubicWave8(milliseconds t, milliseconds period, long minV, long maxV);
float Q_rsqrt(float number);
long cmap(long x, long in_low, long in_high, long out_low, long out_high);

// This picks a random T value when instantiated between min and max (inclusive)
template<typename T, T min, T max>
class RandParam
{
  protected:
    T value;
  public:
    RandParam() { randomize(); }   // including the max
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
template<byte minBpm, byte maxBpm>
class RandSine
{
  protected:
    RandParam<byte, minBpm, maxBpm> bpm;
    RandSign sign;

  public:
    RandSine() { randomize(); }

    void randomize()
    {
      bpm.randomize();
      sign.randomize();
    }

    byte evaluate(long x) { return beatsin8(bpm, 0, 255, 0, sign * x); }
};

#endif
