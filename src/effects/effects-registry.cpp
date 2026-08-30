#include "effects.h"

const EffectInfo EFFECT_REGISTRY[] = {
    {EffectId::AngularPaletteRotation, "Angular Palette Rotation"},
    {EffectId::BezierSwarm, "Bezier Swarm"},
    {EffectId::BouncingBallGlow, "Bouncing Ball Glow"},
    {EffectId::CartesianMoodlight, "Cartesian Moodlight"},
    {EffectId::ElectricSparks, "Electric Sparks"},
    {EffectId::HeatDiffusionRing, "Heat Diffusion Ring"},
    {EffectId::HexagonalRippleGalaxy, "Hexagonal Ripple Galaxy"},
    {EffectId::IndividualStripDrift, "Individual Strip Drift"},
    {EffectId::IndividualStripMoodlight, "Individual Strip Moodlight"},
    {EffectId::MultiPendulum, "Multi Pendulum"},
    {EffectId::NinjaStar, "Ninja Star"},
    {EffectId::PaletteWave, "Palette Wave"},
    {EffectId::PolarMoodlight, "Polar Moodlight"},
    {EffectId::PolarSwipe, "Polar Swipe"},
    {EffectId::RGBodyProblem, "RG Body Problem"},
    {EffectId::SaturationGlow, "Saturation Glow"},
    {EffectId::StandingWaveRing, "Standing Wave Ring"},
};

const size_t NUM_EFFECTS = sizeof(EFFECT_REGISTRY) / sizeof(EFFECT_REGISTRY[0]);

// Instantiates a fresh effect for the given id.
AbstractEffect* createEffect(EffectId id)
{
    switch (id)
    {
        case EffectId::AngularPaletteRotation:
            return new AngularPaletteRotation();
        case EffectId::BezierSwarm:
            return new BezierSwarm();
        case EffectId::BouncingBallGlow:
            return new BouncingBallGlow();
        case EffectId::CartesianMoodlight:
            return new CartesianMoodlight();
        case EffectId::ElectricSparks:
            return new ElectricSparks();
        case EffectId::HeatDiffusionRing:
            return new HeatDiffusionRing();
        case EffectId::HexagonalRippleGalaxy:
            return new HexagonalRippleGalaxy();
        case EffectId::IndividualStripDrift:
            return new IndividualStripDrift();
        case EffectId::IndividualStripMoodlight:
            return new IndividualStripMoodlight();
        case EffectId::MultiPendulum:
            return new MultiPendulum();
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
        case EffectId::StandingWaveRing:
            return new StandingWaveRing();
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
