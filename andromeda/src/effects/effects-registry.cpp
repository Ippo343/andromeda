#include "effects.h"

// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect()
{
    uint8_t EFFECTS_COUNT = 11;

    // Set this to the index of the effect you want to force while testing
    short forcedSelection = -1;

    static uint8_t previousSelection = 255;

    uint8_t selection;
    if (forcedSelection >= 0)
        selection = forcedSelection;
    else
        do selection = random(EFFECTS_COUNT);
        while (selection == previousSelection);

    previousSelection = selection;

    AbstractEffect* retval;
    switch (selection)
    {
        case 0:
            retval = new IndividualStripMoodlight();
            break;
        case 1:
            retval = new ElectricSparks();
            break;
        case 2:
            retval = new SaturationGlow();
            break;
        case 3:
            retval = new PaletteWave();
            break;
        case 4:
            retval = new NinjaStar();
            break;
        case 5:
            retval = new PolarSwipe();
            break;
        case 6:
            retval = new PolarMoodlight();
            break;
        case 7:
            retval = new RGBodyProblem();
            break;
        case 8:
            retval = new HexagonalRippleGalaxy();
            break;
        case 9:
            retval = new IndividualStripDrift();
            break;
        case 10:
            retval = new CartesianMoodlight();
            break;
        default:
            retval = new ErrorEffect();
            break;
    }

    return retval;
}
