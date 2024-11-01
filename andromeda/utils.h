#ifndef ANDROMEDA_UTILS_H
#define ANDROMEDA_UTILS_H

// The adopter module for general utilities,
// like typedefs that I like and small functions reused everywhere

#include <stdint.h> // uint32_t

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
#define LI(idx) ( (idx) % LEDS_PER_STRIP )


void seedRNGs()
{
  // The analog pins not attached to anything, so the voltage fluctuates
  // doing an analog read from it returns noise for the RNG
  randomSeed(analogRead(0));
  random16_set_seed(random(65536));
  random16_add_entropy(random(65536));
}


// Since I want so many randomized parameters,
// might as well overengineer a solution so I don't have to write it every time.
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
    void randomize() override {
      value = random(0, 2) > 0;
    }
};


// Specialized random parameter that can only be -1 or 1, but not 0.
// This is useful as a randomly chosen sign for math operations, e.g:
//    (-1|1) * (led.x)
// This is used in the conveniently homophone class, RandSine
class RandSign : public RandParam<char, -1, 1>
{
  public:
    void randomize() override {
      while (!value)
        value = random(-1, 2);
    }
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


// FastLED's cubicwave8 just maps (0,255)->(0,255)
// The following code first scales the current time into the input range,
// then scales the output into the (-A,A) range
long scaledCubicWave8(milliseconds t, milliseconds period, long minV, long maxV)
{
  milliseconds ct = t % period;
  byte scaledct = map(ct, 0, period, 0, 255);
  byte rawWave = cubicwave8(scaledct);
  long scaledWave = map(rawWave, 0, 255, minV, maxV);
  return scaledWave;
}


// Fast inverse square root algorithm
// Credit: https://en.wikipedia.org/wiki/Fast_inverse_square_root
float Q_rsqrt(float number)
{
  union {
    float    f;
    uint32_t i;
  } conv = { .f = number };
  conv.i  = 0x5f3759df - (conv.i >> 1);
  conv.f *= 1.5F - (number * 0.5F * conv.f * conv.f);
  return conv.f;
}

#endif