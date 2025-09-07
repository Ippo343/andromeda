#include "utils.h"

const char* LOG_FILE_CUR = "/log0.txt";
const char* LOG_FILE_OLD = "/log1.txt";

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
long scaledCubicWave8(milliseconds_t t, milliseconds_t period, long minV, long maxV)
{
  milliseconds_t ct = t % period;
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

float slowSin(unsigned long ms, float bpm, uint8_t minVal, uint8_t maxVal)
{
    float phase = (float(ms) / 1000.0f) * (bpm / 60.0f) * 2.0f * PI;
    float raw = sin(phase); // -1 to 1
    float norm = (raw + 1.0f) / 2.0f; // 0 to 1
    return minVal + norm * (maxVal - minVal);
}

void shuffle(int* array, int size) {
    for (int i = size - 1; i > 0; i--) {
        int j = random(i + 1);  // random index from 0 to i
        int temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}