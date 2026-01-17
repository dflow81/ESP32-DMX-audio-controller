#include "state.h"

// Globale State‑Instanz
ControllerState state;

void initState() {

    // Standardmodus
    state.mode = ControllerState::MODE_SOLID;

    // Master Dimmer
    state.masterDimmer = 255;

    // Strobe
    state.strobeMode = false;
    state.strobeOnFastBass = true;
    state.strobeLevel = 255;

    // Gain / Autogain
    state.gain = 1.0f;
    state.autoGain = true;

    // Audio Werte
    state.level = 0;
    state.bpm = 0;
    state.beat = false;
    state.drop = false;

    // Solid‑Farben (für Modus SOLID)
    state.solidColors[0] = 0xFF0000; // Rot
    state.solidColors[1] = 0x00FF00; // Grün
    state.solidColors[2] = 0x0000FF; // Blau
    state.solidColors[3] = 0xFFFFFF; // Weiß

    // Bunte Standard‑Palette (für Beat Chase & Beat Pulse)
    uint32_t defaultPalette[8] = {
        0xFF0000, // Rot
        0x00FF00, // Grün
        0x0000FF, // Blau
        0xFFFF00, // Gelb
        0xFF00FF, // Magenta
        0x00FFFF, // Cyan
        0xFFA500, // Orange
        0xFFFFFF  // Weiß
    };

    for (int i = 0; i < 8; i++)
        state.palette[i] = defaultPalette[i];

    // DMX Preview initialisieren
    for (int i = 0; i < 32; i++)
        state.dmxPreview[i] = 0;
}
