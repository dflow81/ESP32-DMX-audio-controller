#pragma once
#include <Arduino.h>

struct ControllerState {
    enum Mode : uint8_t {
        MODE_SOLID = 0,
        MODE_BEAT_CHASE = 1,
        MODE_BEAT_PULSE = 2,
        MODE_STROBE = 3
    } mode;

    uint32_t solidColors[4];
    uint32_t palette[8];
    bool useRandomPalette;

    uint8_t masterDimmer;
    bool strobeOnFastBass;
    bool strobeMode;
    uint8_t strobeLevel;

    float gain;
    bool autoGain;

    float level;
    float bpm;
    bool beat;
    bool drop;

    uint8_t dmxPreview[32];
};

extern ControllerState state;

// Audio/Beat‑Globals, die mehrere Dateien brauchen
extern double vReal[];
extern double vImag[];
extern float kickBPM;
extern int currentColorStep;
extern unsigned long dropStrobeUntil;

// Init‑Funktion
void initState();
