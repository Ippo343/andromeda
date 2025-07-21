#include "utils.h"
#include <Arduino.h>
#include <FastLED.h>

void seedRNGs()
{
  // The analog pins not attached to anything, so the voltage fluctuates
  // doing an analog read from it returns noise for the RNG
  randomSeed(analogRead(0));
  random16_set_seed(random(65536));
  random16_add_entropy(random(65536));
}

void RandBool::randomize()
{
  value = random(0, 2) > 0;
}

void RandSign::randomize()
{
  while (!value)
    value = random(-1, 2);
}

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

long cmap(long x, long in_low, long in_high, long out_low, long out_high)
{
  return constrain(map(x, in_low, in_high, out_low, out_high), out_low, out_high);
}
