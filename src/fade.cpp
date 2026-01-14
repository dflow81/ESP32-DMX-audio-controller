#include "fade.h"
#include "audio.h"

// ---------------------------------------------------------
// GLOBAL FADES
// ---------------------------------------------------------

FadeState previewFade;
FadeState beatFade;

// ---------------------------------------------------------
// HELPER: LINEAR INTERPOLATION
// ---------------------------------------------------------

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static uint8_t lerp8(uint8_t a, uint8_t b, float t) {
    return (uint8_t)(a + (b - a) * t);
}

// ---------------------------------------------------------
// START PREVIEW FADE
// ---------------------------------------------------------

void fade_start_preview(const Preset &from, const Preset &to, uint32_t duration) {
    previewFade.active = true;
    previewFade.startTime = millis();
    previewFade.duration = duration;
    previewFade.from = from;
    previewFade.to = to;
}

// ---------------------------------------------------------
// START BEAT FADE
// ---------------------------------------------------------

void fade_start_beat(const Preset &from, const Preset &to) {
    beatFade.active = true;
    beatFade.startTime = millis();
    beatFade.duration = 300;   // short beat fade
    beatFade.from = from;
    beatFade.to = to;
}

// ---------------------------------------------------------
// SHOULD BEAT FADE TRIGGER?
// ---------------------------------------------------------

bool fade_should_trigger_beat() {
    if (!beatFadeEnabled) return false;

    static int beatCounter = 0;

    bool trigger = false;

    if (beatFadeOnKick && kick) trigger = true;
    if (beatFadeOnSnare && snare) trigger = true;
    if (beatFadeOnHiHat && hihat) trigger = true;

    if (!trigger) return false;

    beatCounter++;
    if (beatCounter % beatFadeEvery != 0) return false;

    return true;
}

// ---------------------------------------------------------
// APPLY FADE TO SYSTEM
// ---------------------------------------------------------

static void apply_fade(const FadeState &f, float t) {
    // Program mode
    currentProgram = f.from.program;

    // Palette
    paletteSize = f.from.paletteSize;

    for (int i = 0; i < 8; i++) {
        palette[i].r = lerp8(f.from.palette[i].r, f.to.palette[i].r, t);
        palette[i].g = lerp8(f.from.palette[i].g, f.to.palette[i].g, t);
        palette[i].b = lerp8(f.from.palette[i].b, f.to.palette[i].b, t);
    }

    // Lamp color indices
    for (int i = 0; i < 4; i++) {
        float fi = f.from.lampColorIndex[i];
        float ti = f.to.lampColorIndex[i];
        lampColorIndex[i] = (uint8_t)lerp(fi, ti, t);
    }

    // Dimmer
    masterDimmer = lerp(f.from.masterDimmer, f.to.masterDimmer, t);

    // Gain
    gain = lerp(f.from.gain, f.to.gain, t);
    autoGain = f.to.autoGain;

    // Strobe
    strobeOnBeat = f.to.strobeOnBeat;
    strobeModeEnabled = f.to.strobeModeEnabled;
}

// ---------------------------------------------------------
// PROCESS FADES
// ---------------------------------------------------------

void fade_process() {
    uint32_t now = millis();

    // PREVIEW FADE
    if (previewFade.active) {
        uint32_t dt = now - previewFade.startTime;
        float t = (float)dt / previewFade.duration;

        if (t >= 1.0f) {
            t = 1.0f;
            previewFade.active = false;
        }

        apply_fade(previewFade, t);
        return;
    }

    // BEAT FADE
    if (beatFade.active) {
        uint32_t dt = now - beatFade.startTime;
        float t = (float)dt / beatFade.duration;

        if (t >= 1.0f) {
            t = 1.0f;
            beatFade.active = false;
        }

        apply_fade(beatFade, t);
        return;
    }
}
