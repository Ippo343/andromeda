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
  random16_add_entropy(analogRead(65536));
}

#endif