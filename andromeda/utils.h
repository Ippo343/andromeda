#ifndef ANDROMEDA_UTILS_H
#define ANDROMEDA_UTILS_H

// The adopter module for general utilities,
// like typedefs that I like and small functions reused everywhere

#define byte          uint8_t
#define milliseconds  unsigned long

// I hate typing
#define FOR_EACH_STRIP  for (byte iStrip = 0; iStrip < NUM_STRIPS; iStrip++)
#define FOR_EACH_LED    for (byte iLed = 0; iLed < LEDS_PER_STRIP; iLed++)

byte clamp8(unsigned int x)
{
  return x < 255 ? x : 255;
}

#endif