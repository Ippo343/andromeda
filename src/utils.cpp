#include "utils.h"

#ifndef UNIT_TEST
#include <esp_system.h>  // esp_random()
#endif

const char* LOG_FILE_CUR = "/log0.txt";
const char* LOG_FILE_OLD = "/log1.txt";

void seedRNGs()
{
    // esp_random() is backed by the chip's hardware RNG - no dependency on
    // which GPIO happens to be floating (that varies by board and isn't
    // guaranteed to even be ADC-capable, e.g. GPIO0 on the ESP32-S3).
    randomSeed(esp_random());
    random16_set_seed(random(65536));
    random16_add_entropy(random(65536));
}

uint32_t rateToThreshold(float ratePerSecond, milliseconds_t dt, uint32_t rollLimit)
{
    float p = ratePerSecond * dt / 1000.0f;
    return (uint32_t)(constrain(p, 0.0f, 1.0f) * rollLimit);
}

bool rollEvent(float ratePerSecond, milliseconds_t dt)
{
    constexpr uint32_t ROLL_LIMIT = 100000;
    uint32_t threshold = rateToThreshold(ratePerSecond, dt, ROLL_LIMIT);
    return (uint32_t)random(ROLL_LIMIT) < threshold;
}

uint8_t accumulateFadeAmount(float& debt, float lossRatePerSecond, milliseconds_t dt)
{
    float loss = lossRatePerSecond * dt / 1000.0f;
    debt += constrain(loss, 0.0f, 1.0f) * 255.0f;
    uint8_t fadeAmount = (uint8_t)debt;  // floor
    debt -= fadeAmount;
    return fadeAmount;
}

void RandBool::randomize() { value = random(0, 2) > 0; }

void RandSign::randomize()
{
    while (!value) value = random(-1, 2);
}

// FastLED's cubicwave8 just maps (0,255)->(0,255)
// The following code first scales the current time into the input range,
// then scales the output into the (-A,A) range
long scaledCubicWave8(milliseconds_t t, milliseconds_t period, long minV, long maxV)
{
    milliseconds_t ct = t % period;
    uint8_t scaledct = map(ct, 0, period, 0, 255);
    uint8_t rawWave = cubicwave8(scaledct);
    long scaledWave = map(rawWave, 0, 255, minV, maxV);
    return scaledWave;
}

// Fast inverse square root algorithm
// Credit: https://en.wikipedia.org/wiki/Fast_inverse_square_root
float Q_rsqrt(float number)
{
    union
    {
        float f;
        uint32_t i;
    } conv = {.f = number};
    conv.i = 0x5f3759df - (conv.i >> 1);
    conv.f *= 1.5F - (number * 0.5F * conv.f * conv.f);
    return conv.f;
}

long cmap(long x, long in_low, long in_high, long out_low, long out_high)
{
    // constrain() requires low <= high; a descending output range (out_low > out_high,
    // e.g. IndividualStripDrift's inverted duration ranges) previously constrained
    // every mapped value to the single point out_high except at x == in_low, since
    // constrain(v, out_low, out_high) with out_low > out_high always returns out_high.
    long lo = out_low < out_high ? out_low : out_high;
    long hi = out_low < out_high ? out_high : out_low;
    return constrain(map(x, in_low, in_high, out_low, out_high), lo, hi);
}

float slowSin(unsigned long ms, float bpm, uint8_t minVal, uint8_t maxVal)
{
    float phase = (float(ms) / 1000.0f) * (bpm / 60.0f) * 2.0f * PI;
    float raw = sin(phase);            // -1 to 1
    float norm = (raw + 1.0f) / 2.0f;  // 0 to 1
    return minVal + norm * (maxVal - minVal);
}

void shuffle(int* array, int size)
{
    for (size_t i = size - 1; i > 0; i--)
    {
        int j = random(i + 1);  // random index from 0 to i
        int temp = array[i];
        array[i] = array[j];
        array[j] = temp;
    }
}