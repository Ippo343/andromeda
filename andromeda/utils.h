#ifndef ANDROMEDA_UTILS_H
#define ANDROMEDA_UTILS_H

// The adopter module for general utilities,
// like typedefs that I like and small functions reused everywhere

#define byte          uint8_t
#define milliseconds  unsigned long

// I hate typing
#define FOR_EACH_STRIP  for (byte iStrip = 0; iStrip < NUM_STRIPS; iStrip++)
#define FOR_EACH_LED    for (byte iLed = 0; iLed < LEDS_PER_STRIP; iLed++)

void seedRNGs()
{
  // The analog pins not attached to anything, so the voltage fluctuates
  // doing an analog read from it returns noise for the RNG
  randomSeed(analogRead(0));
  // Let the voltage fluctuate a bit so we don't initialize everything to the same seed.
  // Necessary? Useful? No idea.
  delay(10);
  random16_set_seed(analogRead(1));
  delay(10);
  random16_add_entropy(analogRead(2));
}

#endif