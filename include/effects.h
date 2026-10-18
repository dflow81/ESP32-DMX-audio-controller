#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include <vector>

// --------------------------------------------------
// Effect functions
// --------------------------------------------------

void effectStaticColor();
void effectColorFade();
void effectRainbowWave();
void effectChase();
void effectBounce();
void effectDualWave();
void effectPulse();
void effectSparkle();
void effectStrobe();
void effectFire();
void effectCandle();
void effectAurora();
void effectOcean();
void effectSunset();
void effectFullStrobe();
void effectRandomStrobe();
void effectDimmerWave();
void effectDimmerChase();
void effectDimmerBounce();

// New audio effects
void effectDisco();
void effectDisco2();
void effectLightOrgan();   // Light organ (formerly "retro")
void effectPump();         // Kick → everything bright, then decays
void effectSnareFlash();   // Snare → white flash
void effectBassWave();     // Bass runs as a wave across the lamps

// Current effect step (beat grid), e.g. for the moving heads
long effectStepIndex();

// Manual strobe (button in the web UI), overrides the running effect
void effectManualStrobe();

// Drop reaction (DropAction from state.h), elapsed = ms since the drop
void effectDropReaction(uint8_t action, unsigned long elapsed);

// Helper for timer speed
float getT();

// Central effect control: effect on all fixtures with the base color
void runEffect(const String& name);

// Effect on a selection of fixtures (group) with its own base color.
// slot: 0 = main effect, 1…GROUP_COUNT = group
void runEffectOn(const String& name, const std::vector<int>& fixtures,
                 uint8_t r, uint8_t g, uint8_t b, int slot);

#endif
