#pragma once

// One day we might need more than 8 flags, and I'm not going to search and replace all of them.
// So this is very premature "optimization" to save maybe 10 minutes 3 years from now.
typedef unsigned char control_hints_t;

enum ControlHints : control_hints_t
{
  NONE         = 0,
  ROTATE_SPACE = 1    // Apply a global rotation transform to the whole space
};
