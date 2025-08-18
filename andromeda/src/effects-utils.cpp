#include "effects-utils.h"

const float defaultBrightnessFactor = 255 * 5000;  // * (1 / distance^2)

void paint(CRGB color)
{
  FOR_EACH_STRIP {
    fill_solid(STRIPS[iStrip].buffer, LEDS_PER_STRIP, color);
  }
}

// Paints a single strip with the given color
// Thin wrapper around fill_solid because I'm that lazy
void paintStrip(byte idx, CRGB color)
{
  fill_solid(STRIPS[idx].buffer, LEDS_PER_STRIP, color);
}

// Fade in a strip with the given color over the given duration
void fadeInStrip(byte idx, CHSV color, milliseconds_t duration)
{
  paintStrip(idx, CRGB::Black);
  FASTLED_SHOW();

  milliseconds_t start = millis();
  milliseconds_t dt = 0;
  do
  {
    dt = millis() - start;
    byte v = constrain(map(dt, 0, duration, 0, color.v), 0, 255);
    CHSV c = CHSV(color.h, color.s, v);
    paintStrip(idx, c);
    FASTLED_SHOW();
  }
  while (dt < duration);
}

// Fade in all strips together to the given color in the given duration
void fadeInAllStrips(CHSV color, milliseconds_t duration)
{
  paint(CRGB::Black);
  FASTLED_SHOW();

  milliseconds_t start = millis();
  milliseconds_t dt = 0;
  do
  {
    dt = millis() - start;
    byte v = map(dt, 0, duration, 0, color.v);
    CHSV c = CHSV(color.h, color.s, v);
    FOR_EACH_STRIP {
      paintStrip(iStrip, c);
    }
    FASTLED_SHOW();
  }
  while (dt < duration);
}

// TIL this is the professional approach to generating a random color.
// If you just generate a random RGB triplet the intensity will be all over the place,
// while HSV lets you generate random colors with the same intensity. Cool!
CHSV randomColor()
{
  return CHSV(random8(), 255, 255);
}

// Generate N random complementary colors spaced evenly on the hue wheel
std::vector<CHSV> randomComplementaryColors(byte N)
{
  std::vector<CHSV> retval(N);

  const byte hueStep = (byte)(255.0 / N);
  short a = random8();

  for (byte i = 0; i < N; i++)
  {
    byte h = (a + hueStep * i) % 255;
    retval[i] = CHSV(h, 255, 255);
  }

  return retval;
}

// Pick a random predefined palette
CRGBPalette16 randomPredefinedPalette()
{
  CRGBPalette16 retval;
  switch (random(8))
  {
    case 0:
      retval = CloudColors_p;
      break;
    case 1:
      retval = LavaColors_p;
      break;
    case 2:
      retval = OceanColors_p;
      break;
    case 3:
      retval = ForestColors_p;
      break;
    case 4:
      retval = RainbowColors_p;
      break;
    case 5:
      retval = RainbowStripeColors_p;
      break;
    case 6:
      retval = PartyColors_p;
      break;
    case 7:
      retval = HeatColors_p;
      break;
  }

  return retval;
}

// This factor control the final brightness of each channel.
// 255 is obviously the maximum brightness: but then you need to multiply but some factor
// because otherwise (255 / d^2) is always very very dim.
// I found 5000 by trial and error and it looks good.
byte brightnessFromEmitter(Led* led, CartesianCoordinates e, float brightnessFactor)
{
  short dx = ( led->cartesian.x - e.x );
  short dy = ( led->cartesian.y - e.y );

  // This is the famouse fast inverse square root and boy is it fast.
  // With one single channel, doing everything in floating point with the standard library
  // was tanking the framerate below 40. With the fast algorithm, it runs 3 channels at 75 fps!
  float invdist = Q_rsqrt(dx*dx + dy*dy);
  byte v = (byte)constrain(brightnessFactor * invdist * invdist, 0, 255);

  // Original formula with just the inverse of the distance.
  // Physically correct, but it looks kinda dull.
  // (1/d^2) looks cooler.
  // byte v = (byte)constrain(255 * 30 * invdist , 0, 255);

  return v;
}
