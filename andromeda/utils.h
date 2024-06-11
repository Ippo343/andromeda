#ifndef ANDROMEDA_UTILS_H
#define ANDROMEDA_UTILS_H

// The adopter module for general utilities,
// like typedefs that I like and small functions reused everywhere

#define byte uint8_t

float randFloat()
{
  return random(1000 + 1) / 1000.0;
}

// Scaled Sin function (between 0 and 1, centered around 0.5)
float ssin(float x)
{
  return sin(x) / 2 + 0.5;
}

#endif