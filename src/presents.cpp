#include "presets.h"

// ---------------------------------------------------------
// GLOBAL PRESET STORAGE
// ---------------------------------------------------------

Preset presets[10];
int presetCount = 0;
int activePreset = -1;

// ---------------------------------------------------------
// CURRENT PROGRAM STATE
// ---------------------------------------------------------

ProgramMode currentProgram = PROGRAM_SOLID;

uint8_t paletteSize = 4;
bool paletteRandom = false;

RGB palette[8] = {
    {255, 0, 0},
    {0, 255, 0},
    {0, 0, 255},
    {255, 255, 0},
    {255, 0, 255},
    {0, 255, 255},
    {255, 255, 255},
    {128, 128, 128}
};

uint8_t lampColorIndex[4] = {0, 1, 2, 3};

float gain = 1.0f;
bool autoGain = true;

bool strobeOnBeat = true;
bool strobeModeEnabled = true;

// ---------------------------------------------------------
// CREATE SNAPSHOT OF CURRENT STATE
// ---------------------------------------------------------

Preset makeCurrentPreset(const String &name) {
    Preset p;

    p.name = name;
    p.icon = "🎵";

    p.program = currentProgram;

    p.paletteSize = paletteSize;
    p.paletteRandom = paletteRandom;

    for (int i = 0; i < 8; i++)
        p.palette[i] = palette[i];

    for (int i = 0; i < 4; i++)
        p.lampColorIndex[i] = lampColorIndex[i];

    p.masterDimmer = masterDimmer;

    p.gain = gain;
    p.autoGain = autoGain;

    p.strobeOnBeat = strobeOnBeat;
    p.strobeModeEnabled = strobeModeEnabled;

    return p;
}

// ---------------------------------------------------------
// APPLY PRESET
// ---------------------------------------------------------

void applyPreset(const Preset &p) {
    currentProgram = p.program;

    paletteSize = p.paletteSize;
    paletteRandom = p.paletteRandom;

    for (int i = 0; i < 8; i++)
        palette[i] = p.palette[i];

    for (int i = 0; i < 4; i++)
        lampColorIndex[i] = p.lampColorIndex[i];

    masterDimmer = p.masterDimmer;

    gain = p.gain;
    autoGain = p.autoGain;

    strobeOnBeat = p.strobeOnBeat;
    strobeModeEnabled = p.strobeModeEnabled;
}

// ---------------------------------------------------------
// DEFAULT PRESET
// ---------------------------------------------------------

void presets_init_defaults() {
    presets[0] = makeCurrentPreset("Default");
    presets[0].icon = "🎛️";

    presetCount = 1;
    activePreset = 0;
}
