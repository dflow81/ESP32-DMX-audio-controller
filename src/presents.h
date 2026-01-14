#pragma once
#include <Arduino.h>
#include "dmx.h"

// ---------------------------------------------------------
// PROGRAM MODES
// ---------------------------------------------------------
enum ProgramMode {
    PROGRAM_SOLID = 0,
    PROGRAM_BEAT_COLOR_CYCLE,
    PROGRAM_BEAT_STROBE,
    PROGRAM_RAINBOW_SLOW
};

// ---------------------------------------------------------
// PRESET STRUCT
// ---------------------------------------------------------
struct Preset {
    String name;
    String icon;

    ProgramMode program;

    uint8_t paletteSize;
    bool paletteRandom;

    RGB palette[8];
    uint8_t lampColorIndex[4];

    float masterDimmer;

    float gain;
    bool autoGain;

    bool strobeOnBeat;
    bool strobeModeEnabled;
};

// ---------------------------------------------------------
// GLOBAL PRESET STORAGE
// ---------------------------------------------------------
extern Preset presets[10];
extern int presetCount;
extern int activePreset;

// ---------------------------------------------------------
// CURRENT PROGRAM STATE
// ---------------------------------------------------------
extern ProgramMode currentProgram;

extern uint8_t paletteSize;
extern bool paletteRandom;
extern RGB palette[8];
extern uint8_t lampColorIndex[4];

extern float gain;
extern bool autoGain;

extern bool strobeOnBeat;
extern bool strobeModeEnabled;

// ---------------------------------------------------------
// FUNCTIONS
// ---------------------------------------------------------

// Snapshot of current state → Preset
Preset makeCurrentPreset(const String &name);

// Apply preset to system
void applyPreset(const Preset &p);

// Initialize default preset
void presets_init_defaults();
