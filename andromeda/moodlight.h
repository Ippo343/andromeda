#ifndef MOODLIGHT_H
#define MOODLIGHT_H

// Moodlights are essentially sources of fluctuating colors,
// because each RGB channel is attached to a sine wave.

// Which is admittedly a bit much and doesn't make a huge difference:
// the first test we did showed that having different colors on each led
// is basically indistinguishable from using the whole strip as a moodlight.

class MoodLight
{
  public:
    float TR;
    float TG;
    float TB;

    const float MIN_T = .5f;
    const float T_SPAN = 1.0f;

  void randomize()
  {
    TR = MIN_T + randFloat() * T_SPAN;
    TG = MIN_T + randFloat() * T_SPAN;
    TB = MIN_T + randFloat() * T_SPAN;
  }

  CRGB evaluate(int led, float t)
  {
    byte r = (byte)(255 * ssin(t / TR));
    byte g = (byte)(255 * ssin(t / TG));
    byte b = (byte)(255 * ssin(t / TB));

    return CRGB(r, g, b);
  }
};

#endif