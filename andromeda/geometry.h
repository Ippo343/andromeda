#ifndef ANDROMEDA_GEOMETRY_H
#define ANDROMEDA_GEOMETRY_H

#include "utils.h"

// Data related to the geometry of the mirror,
// e.g. dimensions, number of leds, and so on

const byte LEDS_PER_STRIP = 23;
const byte NUM_STRIPS = 7;

CRGB STRIPS[NUM_STRIPS][LEDS_PER_STRIP];

#endif