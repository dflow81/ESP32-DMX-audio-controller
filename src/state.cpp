#include "state.h"

ControllerState state;   // ← DIE EINZIGE Definition

void initState() {
    state.mode = ControllerState::MODE_BEAT_CHASE;

    state.masterDimmer = 180;
    state.gain = 1.0f;
    state.autoGain = true;

    state.strobeMode = false;
    state.strobeOnFastBass = true;
    state.strobeLevel = 200;

    state.useRandomPalette = false;

    state.solidColors[0] = 0xFF0000;
    state.solidColors[1] = 0x00FF00;
    state.solidColors[2] = 0x0000FF;
    state.solidColors[3] = 0xFFFFFF;

    for (int i = 0; i < 8; i++)
        state.palette[i] = 0xFFFFFF;

    for (int i = 0; i < 32; i++)
        state.dmxPreview[i] = 0;
}
