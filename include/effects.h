#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>
#include <vector>

// --------------------------------------------------
// Effekt-Funktionen
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

// Neue Audio-Effekte
void effectDisco();
void effectDisco2();
void effectLightOrgan();   // Lichtorgel (früher "retro")

// Manueller Strobe (Knopf in der Web-UI), überschreibt den laufenden Effekt
void effectManualStrobe();

// Drop-Reaktion (DropAction aus state.h), elapsed = ms seit dem Drop
void effectDropReaction(uint8_t action, unsigned long elapsed);

// Hilfsfunktion für Timer-Speed
float getT();

// Zentrale Effektsteuerung: Effekt auf alle Fixtures mit der Grundfarbe
void runEffect(const String& name);

// Effekt auf eine Auswahl von Fixtures (Gruppe) mit eigener Grundfarbe.
// slot: 0 = Haupteffekt, 1…GROUP_COUNT = Gruppe
void runEffectOn(const String& name, const std::vector<int>& fixtures,
                 uint8_t r, uint8_t g, uint8_t b, int slot);

#endif
