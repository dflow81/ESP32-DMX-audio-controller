#pragma once

// Taster / Fußschalter am ESP32: jeweils zwischen GPIO und GND (interne Pull-ups).
// Nicht angeschlossene Pins tun nichts. -1 = Funktion ohne Taster.
#define BTN_STROBE_PIN    25   // halten  = Strobe
#define BTN_BLACKOUT_PIN  26   // drücken = Blackout an/aus
#define BTN_FOG_PIN       27   // halten  = Nebel
#define BTN_TAP_PIN       14   // drücken = Tap-Tempo
#define BTN_NEXT_PIN      13   // drücken = nächstes Preset

void setupButtons();

// In loop() aufrufen (entprellt, ~30 ms)
void handleButtons();
