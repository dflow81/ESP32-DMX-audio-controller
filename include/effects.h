#ifndef EFFECTS_H
#define EFFECTS_H

#include <Arduino.h>

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
void effectRetro();

// Manueller Strobe (Knopf in der Web-UI), überschreibt den laufenden Effekt
void effectManualStrobe();

// Hilfsfunktion für Timer-Speed
float getT();

// Zentrale Effektsteuerung
void runEffect(const String& name);

#endif
