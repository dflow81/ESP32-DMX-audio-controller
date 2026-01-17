#pragma once
#include <Arduino.h>

struct ControllerState {
    enum Mode : uint8_t {
        MODE_SOLID = 0,
        MODE_BEAT_CHASE = 1,
        MODE_BEAT_PULSE = 2,
        MODE_STROBE = 3
    } mode;

    // Colors
    uint32_t solidColors[4];
    uint32_t palette[8];
    bool useRandomPalette;

    // Master + Strobe
    uint8_t masterDimmer;
    bool strobeOnFastBass;
    bool strobeMode;
    uint8_t strobeLevel;

    // Audio
    float gain;
    bool autoGain;

    float level;
    float bpm;
    bool beat;
    bool drop;

    // DMX Preview for WebUI
    uint8_t dmxPreview[32];
};

// Global state instance (defined in state.cpp)
extern ControllerState state;

// From audio.cpp (used by dmx.cpp)
extern int currentColorStep;
extern unsigned long dropStrobeUntil;

// Initialize state
void initState();
