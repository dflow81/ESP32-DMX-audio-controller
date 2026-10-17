#include <Arduino.h>
#include "effects.h"
#include "state.h"
#include "dmx.h"
#include <vector>

extern ControllerState state;

// Buffer für Sparkle (wächst mit der Anzahl der Fixtures)
static std::vector<uint8_t> sparkleBuf;

// Zentrale "Uhr" aller Effekte: Position in Effekt-Schritten.
// Normal:    1 Schritt = timerSpeed ms (Speed-Regler)
// Im Takt:   1 Schritt = 1/stepsPerBeat Beat; die Schritte liegen genau auf den
//            erkannten (oder getappten) Beats, weil sie aus dem Beat-Zähler kommen
static double stepPos() {
    if (state.beatSync && state.bpm >= 30) {
        double interval = 60000.0 / state.bpm;
        double frac = (millis() - state.lastBeatMs) / interval; // Anteil bis zum nächsten Beat
        if (frac < 0) frac = 0;
        if (frac > 0.999) frac = 0.999; // nicht über den nächsten (noch nicht erkannten) Beat hinaus
        return (state.beatCount + frac) * state.stepsPerBeat;
    }
    return millis() / (double)max(1, state.timerSpeed);
}

// Aktueller Schritt (für Chase, Bounce …) und Fortschritt innerhalb des Schritts (0…1)
static long   stepIndex() { return (long)stepPos(); }
static double stepPhase() { double p = stepPos(); return p - floor(p); }

// Zeitfaktor für die Wellen-Effekte (gleiche Einheit wie bisher: Schritte)
float getT() {
    return (float)stepPos();
}

// Hilfsfunktion: Ping-Pong-Position (0 … n-1 … 0)
static int bouncePos() {
    int n = state.fixtures.size();
    if (n < 2) return 0;
    int period = 2 * (n - 1);
    int step = stepIndex() % period;
    return step < n ? step : period - step;
}

// --------------------------------------------------
// STATIC COLOR
// --------------------------------------------------
void effectStaticColor() {
    for (int i = 0; i < state.fixtures.size(); i++) {
        setFixtureColor(i, state.colorR, state.colorG, state.colorB, 255);
    }
}

// --------------------------------------------------
// COLOR FADE
// --------------------------------------------------
void effectColorFade() {
    float t = getT();

    uint8_t r = (sin(t) * 127) + 128;
    uint8_t g = (sin(t + 2.09f) * 127) + 128;
    uint8_t b = (sin(t + 4.18f) * 127) + 128;

    for (int i = 0; i < state.fixtures.size(); i++) {
        setFixtureColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// RAINBOW WAVE
// --------------------------------------------------
void effectRainbowWave() {
    float t = getT();

    for (int i = 0; i < state.fixtures.size(); i++) {
        float p = t + i * 0.3f;

        uint8_t r = (sin(p) * 127) + 128;
        uint8_t g = (sin(p + 2.09f) * 127) + 128;
        uint8_t b = (sin(p + 4.18f) * 127) + 128;

        setFixtureColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// CHASE
// --------------------------------------------------
void effectChase() {
    int pos = stepIndex() % state.fixtures.size();

    for (int i = 0; i < state.fixtures.size(); i++) {
        uint8_t v = (i == pos) ? 255 : 0;
        setFixtureColor(i, v, 0, 0, 255);
    }
}

// --------------------------------------------------
// BOUNCE
// --------------------------------------------------
void effectBounce() {
    int pos = bouncePos();

    for (int i = 0; i < state.fixtures.size(); i++) {
        uint8_t v = (i == pos) ? 255 : 0;
        setFixtureColor(i, v, 0, 0, 255);
    }
}

// --------------------------------------------------
// DUAL COLOR WAVE
// --------------------------------------------------
void effectDualWave() {
    float t = getT();

    for (int i = 0; i < state.fixtures.size(); i++) {
        float v = sin(t + i * 0.5f);

        uint8_t r = (v + 1) * 127;
        uint8_t g = (1 - v) * 127;
        uint8_t b = 50;

        setFixtureColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// PULSE
// --------------------------------------------------
void effectPulse() {
    float t = getT();
    uint8_t v = (sin(t) * 127) + 128;

    for (int i = 0; i < state.fixtures.size(); i++) {
        setFixtureColor(i, v, 0, 0, 255);
    }
}

// --------------------------------------------------
// SPARKLE
// --------------------------------------------------
void effectSparkle() {
    if (sparkleBuf.size() < state.fixtures.size()) sparkleBuf.resize(state.fixtures.size(), 0);

    for (int i = 0; i < state.fixtures.size(); i++) {

        if (random(0, 100) < 3)
            sparkleBuf[i] = 255;

        sparkleBuf[i] = sparkleBuf[i] > 5 ? sparkleBuf[i] - 5 : 0;

        setFixtureColor(i, sparkleBuf[i], sparkleBuf[i], sparkleBuf[i], 255);
    }
}

// --------------------------------------------------
// STROBE
// --------------------------------------------------
void effectStrobe() {
    uint8_t v = (stepPhase() < 0.25) ? 255 : 0;

    for (int i = 0; i < state.fixtures.size(); i++) {
        setFixtureColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// FIRE
// --------------------------------------------------
void effectFire() {
    for (int i = 0; i < state.fixtures.size(); i++) {
        uint8_t heat = random(150, 255);

        uint8_t r = heat;
        uint8_t g = heat * 0.4;
        uint8_t b = 0;

        setFixtureColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// CANDLE
// --------------------------------------------------
void effectCandle() {
    for (int i = 0; i < state.fixtures.size(); i++) {
        uint8_t base = 180 + random(-20, 20);
        setFixtureColor(i, base, base * 0.6, 0, 255);
    }
}

// --------------------------------------------------
// AURORA
// --------------------------------------------------
void effectAurora() {
    float t = getT();

    for (int i = 0; i < state.fixtures.size(); i++) {
        float p = sin(t + i * 0.4f);

        uint8_t g = (p + 1) * 127;
        uint8_t b = 200;

        setFixtureColor(i, 0, g, b, 255);
    }
}

// --------------------------------------------------
// OCEAN
// --------------------------------------------------
void effectOcean() {
    float t = getT();

    for (int i = 0; i < state.fixtures.size(); i++) {
        float p = sin(t + i * 0.3f);

        uint8_t b = (p + 1) * 127;
        uint8_t g = b * 0.5;

        setFixtureColor(i, 0, g, b, 255);
    }
}

// --------------------------------------------------
// SUNSET
// --------------------------------------------------
void effectSunset() {
    float t = getT() * 0.2f;

    uint8_t r = 200;
    uint8_t g = (sin(t) * 50) + 80;
    uint8_t b = 0;

    for (int i = 0; i < state.fixtures.size(); i++) {
        setFixtureColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// FULL STROBE
// --------------------------------------------------
void effectFullStrobe() {
    uint8_t v = (stepPhase() < 0.2) ? 255 : 0;

    for (int i = 0; i < state.fixtures.size(); i++) {
        setFixtureColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// RANDOM STROBE
// --------------------------------------------------
void effectRandomStrobe() {
    for (int i = 0; i < state.fixtures.size(); i++) {
        uint8_t v = (random(0, 100) < 10) ? 255 : 0;
        setFixtureColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// DIMMER WAVE
// --------------------------------------------------
void effectDimmerWave() {
    float t = getT();

    for (int i = 0; i < state.fixtures.size(); i++) {
        uint8_t v = (sin(t + i * 0.4f) * 127) + 128;
        setFixtureColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// DIMMER CHASE
// --------------------------------------------------
void effectDimmerChase() {
    int pos = stepIndex() % state.fixtures.size();

    for (int i = 0; i < state.fixtures.size(); i++) {
        uint8_t v = (i == pos) ? 255 : 0;
        setFixtureColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// DIMMER BOUNCE
// --------------------------------------------------
void effectDimmerBounce() {
    int pos = bouncePos();

    for (int i = 0; i < state.fixtures.size(); i++) {
        uint8_t v = (i == pos) ? 255 : 0;
        setFixtureColor(i, v, v, v, 255);
    }
}

//
// --------------------------------------------------
// DISCO (Audio Beat → Farbwechsel)
// --------------------------------------------------
void effectDisco() {
    // (Drops werden zentral behandelt, siehe effectDropReaction)

    // --- NORMALER DISCO BEAT ---
    if (state.beat) {
        // Bei jedem Schlag eine neue Zufallsfarbe für alle
        state.colorR = random(0, 255);
        state.colorG = random(0, 255);
        state.colorB = random(0, 255);
    }

    // Farbe auf alle Fixtures anwenden
    for (int i = 0; i < state.fixtures.size(); i++) {
        setFixtureColor(i, state.colorR, state.colorG, state.colorB, 255);
    }
}

void effectDisco2() {
    // 1. Die 8 vordefinierten, kräftigen Farben
    static const uint8_t palette[8][3] = {
        {255, 0, 0},   // Rot
        {0, 255, 0},   // Grün
        {0, 0, 255},   // Blau
        {255, 255, 0}, // Gelb
        {255, 0, 255}, // Magenta
        {0, 255, 255}, // Cyan
        {255, 127, 0}, // Orange
        {255, 50, 50}  // Pastell-Rot
    };

    // Speichert, welche Farbe gerade für welches Fixture aktiv ist
    static std::vector<int> fixtureColorIdx;

    if (fixtureColorIdx.size() < state.fixtures.size()) fixtureColorIdx.resize(state.fixtures.size(), 0);

    // (Drops werden zentral behandelt, siehe effectDropReaction)

    // --- NORMALER BEAT (Farbwechsel) ---
    if (state.beat) {
        for (int i = 0; i < state.fixtures.size(); i++) {
            fixtureColorIdx[i] = random(0, 8); // Neue Zufallsfarbe aus Palette
        }
    }

    // Farben auf Lampen anwenden
    for (int i = 0; i < state.fixtures.size(); i++) {
        int idx = fixtureColorIdx[i];
        setFixtureColor(i, palette[idx][0], palette[idx][1], palette[idx][2], 255);
    }
}


//
// --------------------------------------------------
void effectRetro() {
    // 1. Schwellenwerte massiv erhöhen (basierend auf deinen K:100000, S:30000, H:80000 Werten)
    // Wir teilen durch den ungefähren Maximalwert, damit wir bei 1.0 landen.
    float low   = constrain(state.kickEnergy  / 180000.0f, 0.0f, 1.0f);
    float mid   = constrain(state.snareEnergy / 190000.0f, 0.0f, 1.0f);
    float high  = constrain(state.hihatEnergy / 170000.0f, 0.0f, 1.0f);

    // 2. Ein "Noise Gate" einbauen: Wenn der Wert zu klein ist, bleibt es aus.
    // Das verhindert das Flackern bei Stille (da deine Hi-Hats selbst bei "Beat:0" auf 70.000 sind)
    if (low < 0.2f)  low = 0; 
    if (mid < 0.2f)  mid = 0;
    if (high < 0.3f) high = 0; // Hi-Hats brauchen ein höheres Gate, da sie sehr präsent sind

    int numFixtures = state.fixtures.size();
    for (int i = 0; i < numFixtures; i++) {
        int band = i % 3; 

        if (band == 0) {
            // pow(..., 3.0f) macht den Kontrast extrem hart: 
            // Nur die echten Spitzen (Kicks) lassen die Lampe hell aufleuchten.
            uint8_t r = pow(low, 3.0f) * 255;
            setFixtureColor(i, r, 0, 0, 255); 
        } 
        else if (band == 1) {
            uint8_t g = pow(mid, 3.0f) * 255;
            setFixtureColor(i, 0, g, 0, 255);
        } 
        else {
            uint8_t b = pow(high, 3.0f) * 255;
            setFixtureColor(i, 0, 0, b, 255);
        }
    }
}





// --------------------------------------------------
// DROP-REAKTION (überschreibt den Effekt für DROP_HOLD_MS)
// --------------------------------------------------
void effectDropReaction(uint8_t action, unsigned long elapsed) {
    int n = state.fixtures.size();

    if (action == DROP_FLASH) {
        // Weißer Blitz, der über die Drop-Dauer ausblendet
        float k = 1.0f - min(1.0f, elapsed / (float)DROP_HOLD_MS);
        uint8_t v = k * k * 255; // quadratisch → kräftiger Anfang, weiches Ende
        for (int i = 0; i < n; i++) setFixtureColor(i, v, v, v, 255);
        return;
    }

    if (action == DROP_COLORBURST) {
        // Schnell wechselnde, kräftige Farben – jede Lampe anders
        static const uint8_t palette[6][3] = {
            {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {255, 0, 255}, {0, 255, 255}
        };
        static std::vector<uint8_t> idx;
        static unsigned long lastChange = 0;
        if (idx.size() < (size_t)n) idx.resize(n, 0);
        if (millis() - lastChange > 80) {
            for (int i = 0; i < n; i++) idx[i] = random(0, 6);
            lastChange = millis();
        }
        for (int i = 0; i < n; i++) setFixtureColor(i, palette[idx[i]][0], palette[idx[i]][1], palette[idx[i]][2], 255);
        return;
    }

    // DROP_STROBE (Standard): alle Lampen synchron weiß/aus, alle 40 ms Wechsel
    uint8_t s = ((millis() / 40) % 2 == 0) ? 255 : 0;
    for (int i = 0; i < n; i++) setFixtureColor(i, s, s, s, 255);
}

// --------------------------------------------------
// MANUELLER STROBE (Knopf in der Web-UI)
// --------------------------------------------------
void effectManualStrobe() {
    // Feste ~12 Hz, unabhängig vom Effekt-Speed (wie der Drop-Strobe)
    uint8_t s = ((millis() / 40) % 2 == 0) ? 255 : 0;
    for (int i = 0; i < state.fixtures.size(); i++) {
        setFixtureColor(i, s, s, s, 255);
    }
}

// --------------------------------------------------
// RUN EFFECT (Zentrale Steuerung)
// --------------------------------------------------
void runEffect(const String& name) {
    // Ohne Fixtures nichts tun (verhindert Division durch 0 in Chase-Effekten)
    if (state.fixtures.empty()) return;

    if (name == "static") effectStaticColor();
    else if (name == "fade") effectColorFade();
    else if (name == "rainbow") effectRainbowWave();
    else if (name == "chase") effectChase();
    else if (name == "bounce") effectBounce();
    else if (name == "dualwave") effectDualWave();
    else if (name == "pulse") effectPulse();
    else if (name == "sparkle") effectSparkle();
    else if (name == "strobe") effectStrobe();
    else if (name == "fire") effectFire();
    else if (name == "candle") effectCandle();
    else if (name == "aurora") effectAurora();
    else if (name == "ocean") effectOcean();
    else if (name == "sunset") effectSunset();
    else if (name == "fullstrobe") effectFullStrobe();
    else if (name == "randomstrobe") effectRandomStrobe();
    else if (name == "dimmerwave") effectDimmerWave();
    else if (name == "dimmerchase") effectDimmerChase();
    else if (name == "dimmerbounce") effectDimmerBounce();
    else if (name == "disco") effectDisco();
    else if (name == "disco2") effectDisco2();
    else if (name == "retro") effectRetro();
}
