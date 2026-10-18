#include <Arduino.h>
#include "buttons.h"
#include "state.h"

#define DEBOUNCE_MS    30
#define HOLD_REFRESH_MS 150   // Strobe/Nebel laufen weiter, solange gedrückt

enum ButtonAction { ACT_STROBE, ACT_BLACKOUT, ACT_FOG, ACT_TAP, ACT_NEXT };

struct Button {
    int8_t pin;
    ButtonAction action;
    bool pressed;            // entprellter Zustand
    bool lastRaw;
    unsigned long changedMs;
};

static Button buttons[] = {
    {BTN_STROBE_PIN,   ACT_STROBE,   false, false, 0},
    {BTN_BLACKOUT_PIN, ACT_BLACKOUT, false, false, 0},
    {BTN_FOG_PIN,      ACT_FOG,      false, false, 0},
    {BTN_TAP_PIN,      ACT_TAP,      false, false, 0},
    {BTN_NEXT_PIN,     ACT_NEXT,     false, false, 0},
};

void setupButtons() {
    for (auto &b : buttons) {
        if (b.pin < 0) continue;
        pinMode(b.pin, INPUT_PULLUP);
    }
}

// Einmal beim Drücken
static void onPress(ButtonAction a) {
    state.lastUserMs = millis();
    switch (a) {
        case ACT_BLACKOUT:
            state.blackout = !state.blackout;
            break;
        case ACT_TAP:
            registerTap();
            break;
        case ACT_NEXT: {
            StateLock lock;
            if (!state.presets.empty()) {
                int next = (state.lastPresetIndex + 1) % state.presets.size();
                applyPreset(next, true);
            }
            break;
        }
        default:
            break;
    }
}

void handleButtons() {
    unsigned long now = millis();
    for (auto &b : buttons) {
        if (b.pin < 0) continue;
        bool raw = digitalRead(b.pin) == LOW;   // gedrückt = nach GND
        if (raw != b.lastRaw) {
            b.lastRaw = raw;
            b.changedMs = now;
        }
        if (raw != b.pressed && now - b.changedMs >= DEBOUNCE_MS) {
            b.pressed = raw;
            if (b.pressed) onPress(b.action);
        }

        // Halten-Funktionen laufend auffrischen (enden kurz nach dem Loslassen)
        if (b.pressed) {
            if (b.action == ACT_STROBE) state.manualStrobeUntil = now + HOLD_REFRESH_MS;
            if (b.action == ACT_FOG)    state.manualFogUntil = now + HOLD_REFRESH_MS;
            state.lastUserMs = now;
        }
    }
}
