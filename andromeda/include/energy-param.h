#pragma once

#include "utils.h"

/*
 * Hello, future me!
 * Do you remember how long it took to figure out the build for this darned thing?
 * I wanted EnergyParam to automatically convert the global energy value to a range,
 * but I couldn't solve the circular dependencies.
 * So I spent several evenings with ChatGPT and Claude to untangle the headers,
 * only to discover that it was solving absolutely nothing at all.
 * So here is the final accrocchio that I landed on: instead of the MissionControl
 * holding the energy value, we have a static Energy class that holds the value instead.
 * My naive C# brain hoped that it could be a static member of EnergyParam, but no!
 * Because each instance of EnergyParam is a different type, so it cannot be static.
 * And eventually here we are: a glorified global variable with a fancy suit on.
 * But it has accessors, so it's professional.
 */
class Energy
{
  public:
    inline static byte get() { return value; }
    inline static void set(byte v) { value = constrain(v, 0, 255); }

  private:
    static byte value;
};


/*
 * EnergyParam is a template class that maps the global energy value to its range.
 * Whenever its value is read, it maps the global energy implicitly
 * This should let effects automatically and continuously adapt to the energy level,
 * initially set via the web interface and one day hopefully via a microphone.
 */
template <typename T, T min, T max>
class EnergyParam
{
  public:

    inline operator T() const
    {
        return cmap(Energy::get(), 0, 255, min, max);
    }

};
