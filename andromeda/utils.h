#ifndef ANDROMEDA_UTILS_H
#define ANDROMEDA_UTILS_H

// The adopter module for general utilities,
// like typedefs that I like and small functions reused everywhere

#define byte          uint8_t
#define milliseconds  unsigned long

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
    RandParam() { value = random(min, max + 1); }   // including the max
    inline operator T() const { return value; }
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

#endif