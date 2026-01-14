#pragma once
#include <Arduino.h>
#include "presets.h"

// ---------------------------------------------------------
// FADE STRUCT
// ---------------------------------------------------------
struct FadeState {
    bool active = false;

    uint32_t startTime = 0;
    uint32_t duration = 0;

    Preset from;
    Preset to;
};

// ---------------------------------------------------------
// GLOBAL FADES
// ---------------------------------------------------------

// Preview fade (hover in UI)
extern FadeState previewFade;

// Beat fade (triggered by kick/snare/hihat)
extern FadeState beatFade;

// ---------------------------------------------------------
// FUNCTIONS
// ---------------------------------------------------------

// Start a preview fade (UI hover)
void fade_start_preview(const Preset &from, const Preset &to, uint32_t duration);

// Start a beat fade
void fade_start_beat(const Preset &from, const Preset &to);

// Process fade each frame
void fade_process();

// Should a beat fade trigger?
bool fade_should_trigger_beat();
