#ifndef MOODLIGHT_H
#define MOODLIGHT_H

// Moodlights are essentially sources of fluctuating colors,
// because each RGB channel is attached to a sine wave.
// To make things even more spacy I am also using different values
// for both the time and space period of each channel chosen at random.

// Which is admittedly a bit much and doesn't make a huge difference:
// the first test we did showed that having different colors on each led
// is basically indistinguishable from using the whole strip as a moodlight.

class MoodLight
{
  public:
    float TR;
    float TG;
    float TB;

    float LR;
    float LG;
    float LB;

    const float MIN_T = .5f;
    const float T_SPAN = 1.0f;
    const float L_SCALE = 1.0f;

  void randomize()
  {
    TR = MIN_T + randFloat() * T_SPAN;
    TG = MIN_T + randFloat() * T_SPAN;
    TB = MIN_T + randFloat() * T_SPAN;

    LR = randFloat() * LEDS_PER_STRIP * 1.0f;
    LG = randFloat() * LEDS_PER_STRIP * 1.0f;
    LB = randFloat() * LEDS_PER_STRIP * 1.0f;
  }

  CRGB evaluate(int led, float t)
  {
    byte r = (byte)(255 * ssin(led / LR + t / TR));
    byte g = (byte)(255 * ssin(led / LG + t / TG));
    byte b = (byte)(255 * ssin(led / LB + t / TB));

    return CRGB(r, g, b);
  }
};

#endif