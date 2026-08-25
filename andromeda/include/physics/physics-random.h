#pragma once

// RandParam<T,min,max> (utils.h) only handles integral ranges. Physics needs
// continuous float ranges (masses, angles, speeds, control-point offsets), so this
// builds a uniform float draw on top of Arduino's random(long,long).

#include <Arduino.h>

// Uniform random float in [min, max). Arduino's random(long,long) is exclusive of its
// upper bound, so this is too.
inline float randomFloat(float min, float max)
{
    const long RESOLUTION = 1000000L;
    long r = random(0, RESOLUTION);
    return min + (max - min) * ((float)r / (float)RESOLUTION);
}
