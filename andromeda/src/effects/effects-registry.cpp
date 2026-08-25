#include "effects.h"

const EffectInfo EFFECT_REGISTRY[] = {
    {EffectId::BezierSwarm, "BezierSwarm"},
    {EffectId::CartesianMoodlight, "CartesianMoodlight"},
    {EffectId::ElectricSparks, "ElectricSparks"},
    {EffectId::HexagonalRippleGalaxy, "Hexagonal Ripple Galaxy"},
    {EffectId::IndividualStripDrift, "IndividualStripDrift"},
    {EffectId::IndividualStripMoodlight, "IndividualStripMoodlight"},
    {EffectId::NinjaStar, "NinjaStar"},
    {EffectId::PaletteWave, "PaletteWave"},
    {EffectId::PolarMoodlight, "PolarMoodlight"},
    {EffectId::PolarSwipe, "PolarSwipe"},
    {EffectId::RGBodyProblem, "RGBodyProblem"},
    {EffectId::SaturationGlow, "SaturationGlow"},
};

const size_t NUM_EFFECTS = sizeof(EFFECT_REGISTRY) / sizeof(EFFECT_REGISTRY[0]);

// Instantiates a fresh effect for the given id.
AbstractEffect* createEffect(EffectId id)
{
    switch (id)
    {
        case EffectId::BezierSwarm:
            return new BezierSwarm();
        case EffectId::CartesianMoodlight:
            return new CartesianMoodlight();
        case EffectId::ElectricSparks:
            return new ElectricSparks();
        case EffectId::HexagonalRippleGalaxy:
            return new HexagonalRippleGalaxy();
        case EffectId::IndividualStripDrift:
            return new IndividualStripDrift();
        case EffectId::IndividualStripMoodlight:
            return new IndividualStripMoodlight();
        case EffectId::NinjaStar:
            return new NinjaStar();
        case EffectId::PaletteWave:
            return new PaletteWave();
        case EffectId::PolarMoodlight:
            return new PolarMoodlight();
        case EffectId::PolarSwipe:
            return new PolarSwipe();
        case EffectId::RGBodyProblem:
            return new RGBodyProblem();
        case EffectId::SaturationGlow:
            return new SaturationGlow();
    }
    return new ErrorEffect();
}

// Set this to force a specific effect while testing (e.g. EffectId::NinjaStar).
// EffectId::None means "no forced selection" - see its definition in effects.h.
constexpr EffectId forcedSelection = EffectId::None;

// Picks a new random effect and randomizes it
AbstractEffect* getRandomEffect()
{
    static uint8_t previousSelection = 255;

    uint8_t selection;
    if (forcedSelection != EffectId::None)
        selection = static_cast<uint8_t>(forcedSelection);
    else
        do selection = random(NUM_EFFECTS);
        while (selection == previousSelection);

    previousSelection = selection;

    return createEffect(EFFECT_REGISTRY[selection].id);
}
