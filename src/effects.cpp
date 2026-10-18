#include <Arduino.h>
#include "effects.h"
#include "state.h"
#include "dmx.h"
#include <vector>

extern ControllerState state;

// --------------------------------------------------
// Effekt-Kontext: Effekte laufen auf einer Auswahl von Fixtures
// (Haupteffekt oder eine Gruppe) mit der Grundfarbe dieser Auswahl.
// Index k in den Effekten ist lokal (0 … fxCount()-1).
// --------------------------------------------------
struct EffectCtx {
    const std::vector<int> *idx;
    uint8_t r, g, b;
    int slot;   // 0 = Haupteffekt, 1…GROUP_COUNT = Gruppe (für Effekt-interne Zustände)
};
static EffectCtx ctx = {nullptr, 255, 0, 0, 0};
static std::vector<int> allFixtures;

static int  fxCount()      { return ctx.idx ? ctx.idx->size() : 0; }
static int  fxGlobal(int k) { return (*ctx.idx)[k]; }
static void fxColor(int k, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    setFixtureColor(fxGlobal(k), r, g, b, w);
}

// Kontext auf alle Fixtures + Grundfarbe setzen (Strobe, Drop)
static void useAllFixtures() {
    allFixtures.resize(state.fixtures.size());
    for (size_t i = 0; i < allFixtures.size(); i++) allFixtures[i] = i;
    ctx = {&allFixtures, state.colorR, state.colorG, state.colorB, 0};
}

// --------------------------------------------------
// Farbpalette: Position (in Paletten-Farben) → Farbe, weich überblendet und zyklisch
// --------------------------------------------------
static void paletteAt(float pos, uint8_t &r, uint8_t &g, uint8_t &b) {
    const Palette &p = state.palette;
    int n = constrain((int)p.size, 2, PALETTE_MAX);
    pos = fmodf(pos, (float)n);
    if (pos < 0) pos += n;
    int i = (int)pos;
    float f = pos - i;
    const uint8_t *a = p.c[i], *c = p.c[(i + 1) % n];
    r = a[0] + (c[0] - a[0]) * f;
    g = a[1] + (c[1] - a[1]) * f;
    b = a[2] + (c[2] - a[2]) * f;
}

// Zufällige Paletten-Farbe (ohne Überblenden)
static void paletteRandom(uint8_t &r, uint8_t &g, uint8_t &b) {
    int n = constrain((int)state.palette.size, 2, PALETTE_MAX);
    const uint8_t *c = state.palette.c[random(0, n)];
    r = c[0]; g = c[1]; b = c[2];
}

static void runEffectNamed(const String& name);

// Buffer für Sparkle (Index = globaler Fixture-Index)
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

// Effekt-Parameter: Breite (leuchtende Lampen) und Intensität (k = 1.0 bei 50 %)
static int   fxWidth() { return constrain((int)state.fxWidth, 1, 8); }
static float fxK()     { return state.fxIntensity / 50.0f; }

// Lampe k liegt im "Fenster" der Breite fxWidth() ab Position pos (mit Umlauf)
static bool inWindow(int k, int pos) {
    int n = fxCount();
    int d = ((k - pos) % n + n) % n;
    return d < fxWidth();
}

// Für die Moving Heads: aktueller Effekt-Schritt (Takt-Raster)
long effectStepIndex() { return stepIndex(); }

// Auslöser für Farbwechsel der Disco-Effekte:
// "Im Takt" an → bei jedem neuen Effekt-Schritt (½ / 1 / 2 Beats, wie eingestellt),
// sonst wie bisher bei jedem erkannten Beat. Merkt sich den Schritt je Effekt und Slot.
static bool colorChangeDue(int effectId) {
    static long lastStep[3][GROUP_COUNT + 1];   // 0 = Disco, 1 = Disco2, 2 = Pump
    static bool init = false;
    if (!init) { for (auto &row : lastStep) for (long &v : row) v = -1; init = true; }

    if (!(state.beatSync && state.bpm >= 30)) return state.beat;
    long step = stepIndex();
    long &last = lastStep[effectId][ctx.slot];
    if (step == last) return false;
    last = step;
    return true;
}
static double stepPhase() { double p = stepPos(); return p - floor(p); }

// Zeitfaktor für die Wellen-Effekte (gleiche Einheit wie bisher: Schritte)
float getT() {
    return (float)stepPos();
}

// Hilfsfunktion: Ping-Pong-Position (0 … n-1 … 0)
static int bouncePos() {
    int n = fxCount();
    if (n < 2) return 0;
    int period = 2 * (n - 1);
    int step = stepIndex() % period;
    return step < n ? step : period - step;
}

// --------------------------------------------------
// STATIC COLOR
// --------------------------------------------------
void effectStaticColor() {
    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, ctx.r, ctx.g, ctx.b, 255);
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
    // Palette: gleiche Geschwindigkeit, aber durch die Paletten-Farben statt durch den Farbkreis
    if (state.palette.on) paletteAt(t / TWO_PI * state.palette.size, r, g, b);

    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// RAINBOW WAVE
// --------------------------------------------------
void effectRainbowWave() {
    float t = getT();

    for (int i = 0; i < fxCount(); i++) {
        float p = t + i * 0.3f * fxK();

        uint8_t r = (sin(p) * 127) + 128;
        uint8_t g = (sin(p + 2.09f) * 127) + 128;
        uint8_t b = (sin(p + 4.18f) * 127) + 128;
        if (state.palette.on) paletteAt(p / TWO_PI * state.palette.size, r, g, b);

        fxColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// CHASE
// --------------------------------------------------
// Farbe für Chase/Bounce/Pulse: Rot, mit Palette pro Durchlauf die nächste Farbe
static void runnerColor(long round, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = 255; g = 0; b = 0;
    if (state.palette.on) {
        int n = constrain((int)state.palette.size, 2, PALETTE_MAX);
        const uint8_t *c = state.palette.c[round % n];
        r = c[0]; g = c[1]; b = c[2];
    }
}

void effectChase() {
    long step = stepIndex();
    int pos = step % fxCount();
    uint8_t r, g, b;
    runnerColor(step / fxCount(), r, g, b);

    for (int i = 0; i < fxCount(); i++) {
        if (inWindow(i, pos)) fxColor(i, r, g, b, 255);
        else                  fxColor(i, 0, 0, 0, 255);
    }
}

// --------------------------------------------------
// BOUNCE
// --------------------------------------------------
void effectBounce() {
    int pos = bouncePos();
    uint8_t r, g, b;
    runnerColor(stepIndex() / max(1, 2 * (fxCount() - 1)), r, g, b);

    for (int i = 0; i < fxCount(); i++) {
        if (inWindow(i, pos)) fxColor(i, r, g, b, 255);
        else                  fxColor(i, 0, 0, 0, 255);
    }
}

// --------------------------------------------------
// DUAL COLOR WAVE
// --------------------------------------------------
void effectDualWave() {
    float t = getT();

    for (int i = 0; i < fxCount(); i++) {
        float v = sin(t + i * 0.5f * fxK());

        uint8_t r = (v + 1) * 127;
        uint8_t g = (1 - v) * 127;
        uint8_t b = 50;
        if (state.palette.on) {
            // zwischen Paletten-Farbe 1 und 2 hin und her
            float f = (v + 1) / 2;
            const uint8_t *a = state.palette.c[0], *c = state.palette.c[1];
            r = a[0] + (c[0] - a[0]) * f;
            g = a[1] + (c[1] - a[1]) * f;
            b = a[2] + (c[2] - a[2]) * f;
        }

        fxColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// PULSE
// --------------------------------------------------
void effectPulse() {
    float t = getT();
    uint8_t v = (sin(t) * 127) + 128;
    uint8_t r, g, b;
    runnerColor((long)(t / TWO_PI), r, g, b); // mit Palette: pro Puls die nächste Farbe

    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, r * v / 255, g * v / 255, b * v / 255, 255);
    }
}

// --------------------------------------------------
// SPARKLE
// --------------------------------------------------
void effectSparkle() {
    if (sparkleBuf.size() < state.fixtures.size()) sparkleBuf.resize(state.fixtures.size(), 0);

    for (int i = 0; i < fxCount(); i++) {
        uint8_t &s = sparkleBuf[fxGlobal(i)];

        if (random(0, 1000) < 30 * fxK())     // Dichte (3 % pro Bild bei 50)
            s = 255;

        s = s > 5 ? s - 5 : 0;

        fxColor(i, s, s, s, 255);
    }
}

// --------------------------------------------------
// STROBE
// --------------------------------------------------
void effectStrobe() {
    uint8_t v = (stepPhase() < constrain(0.25f * fxK(), 0.02f, 0.9f)) ? 255 : 0;  // Blitzlänge

    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// FIRE
// --------------------------------------------------
void effectFire() {
    for (int i = 0; i < fxCount(); i++) {
        int low = constrain((int)(255 - 105 * fxK()), 0, 254);   // Flackerstärke
        uint8_t heat = random(low, 255);

        uint8_t r = heat;
        uint8_t g = heat * 0.4;
        uint8_t b = 0;

        fxColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// CANDLE
// --------------------------------------------------
void effectCandle() {
    for (int i = 0; i < fxCount(); i++) {
        int f = 20 * fxK();                                      // Flackerstärke
        uint8_t base = constrain(180 + (int)random(-f, f + 1), 0, 255);
        fxColor(i, base, base * 0.6, 0, 255);
    }
}

// --------------------------------------------------
// AURORA
// --------------------------------------------------
void effectAurora() {
    float t = getT();

    for (int i = 0; i < fxCount(); i++) {
        float p = sin(t + i * 0.4f * fxK());

        uint8_t g = (p + 1) * 127;
        uint8_t b = 200;

        fxColor(i, 0, g, b, 255);
    }
}

// --------------------------------------------------
// OCEAN
// --------------------------------------------------
void effectOcean() {
    float t = getT();

    for (int i = 0; i < fxCount(); i++) {
        float p = sin(t + i * 0.3f * fxK());

        uint8_t b = (p + 1) * 127;
        uint8_t g = b * 0.5;

        fxColor(i, 0, g, b, 255);
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

    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, r, g, b, 255);
    }
}

// --------------------------------------------------
// FULL STROBE
// --------------------------------------------------
void effectFullStrobe() {
    uint8_t v = (stepPhase() < constrain(0.2f * fxK(), 0.02f, 0.9f)) ? 255 : 0;   // Blitzlänge

    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// RANDOM STROBE
// --------------------------------------------------
void effectRandomStrobe() {
    for (int i = 0; i < fxCount(); i++) {
        uint8_t v = (random(0, 1000) < 100 * fxK()) ? 255 : 0;   // Dichte
        fxColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// DIMMER WAVE
// --------------------------------------------------
void effectDimmerWave() {
    float t = getT();

    for (int i = 0; i < fxCount(); i++) {
        uint8_t v = (sin(t + i * 0.4f * fxK()) * 127) + 128;
        fxColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// DIMMER CHASE
// --------------------------------------------------
void effectDimmerChase() {
    int pos = stepIndex() % fxCount();

    for (int i = 0; i < fxCount(); i++) {
        uint8_t v = inWindow(i, pos) ? 255 : 0;
        fxColor(i, v, v, v, 255);
    }
}

// --------------------------------------------------
// DIMMER BOUNCE
// --------------------------------------------------
void effectDimmerBounce() {
    int pos = bouncePos();

    for (int i = 0; i < fxCount(); i++) {
        uint8_t v = inWindow(i, pos) ? 255 : 0;
        fxColor(i, v, v, v, 255);
    }
}

//
// --------------------------------------------------
// DISCO (Audio Beat → Farbwechsel)
// --------------------------------------------------
void effectDisco() {
    // (Drops werden zentral behandelt, siehe effectDropReaction)

    // Aktuelle Disco-Farbe je Haupteffekt/Gruppe
    static uint8_t color[GROUP_COUNT + 1][3] = {{255, 0, 0}};
    uint8_t *c = color[ctx.slot];

    // --- NORMALER DISCO BEAT (bzw. Schritt, wenn "Im Takt" an ist) ---
    if (colorChangeDue(0)) {
        // Bei jedem Schlag eine neue Zufallsfarbe für alle (mit Palette: aus der Palette)
        if (state.palette.on) paletteRandom(c[0], c[1], c[2]);
        else { c[0] = random(0, 255); c[1] = random(0, 255); c[2] = random(0, 255); }
    }

    // Farbe auf alle Fixtures anwenden
    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, c[0], c[1], c[2], 255);
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

    // Eigene Palette statt der 8 festen Farben, wenn eingeschaltet
    bool own = state.palette.on;
    int count = own ? constrain((int)state.palette.size, 2, PALETTE_MAX) : 8;

    // --- NORMALER BEAT (Farbwechsel; bzw. Schritt, wenn "Im Takt" an ist) ---
    if (colorChangeDue(1)) {
        for (int i = 0; i < fxCount(); i++) {
            fixtureColorIdx[fxGlobal(i)] = random(0, count); // Neue Zufallsfarbe aus Palette
        }
    }

    // Farben auf Lampen anwenden
    for (int i = 0; i < fxCount(); i++) {
        int idx = fixtureColorIdx[fxGlobal(i)] % count;
        const uint8_t *c = own ? state.palette.c[idx] : palette[idx];
        fxColor(i, c[0], c[1], c[2], 255);
    }
}


//
// --------------------------------------------------
// LIGHT ORGAN (Lichtorgel, früher "retro"): Bass rot, Mitten grün, Höhen blau
void effectLightOrgan() {
    // 1. Auf "laut" normieren (Referenzen aus dem Einmessen, sonst feste Erfahrungswerte),
    // damit wir bei 1.0 landen
    float low   = constrain(state.kickEnergy  / max(1.0, state.refKick),  0.0, 1.0);
    float mid   = constrain(state.snareEnergy / max(1.0, state.refSnare), 0.0, 1.0);
    float high  = constrain(state.hihatEnergy / max(1.0, state.refHihat), 0.0, 1.0);

    // 2. Ein "Noise Gate" einbauen: Wenn der Wert zu klein ist, bleibt es aus.
    // Das verhindert das Flackern bei Stille (da deine Hi-Hats selbst bei "Beat:0" auf 70.000 sind)
    if (low < 0.2f)  low = 0; 
    if (mid < 0.2f)  mid = 0;
    if (high < 0.3f) high = 0; // Hi-Hats brauchen ein höheres Gate, da sie sehr präsent sind

    int numFixtures = fxCount();
    for (int i = 0; i < numFixtures; i++) {
        int band = i % 3; 

        if (band == 0) {
            // pow(..., 3.0f) macht den Kontrast extrem hart: 
            // Nur die echten Spitzen (Kicks) lassen die Lampe hell aufleuchten.
            uint8_t r = pow(low, 3.0f) * 255;
            fxColor(i, r, 0, 0, 255); 
        } 
        else if (band == 1) {
            uint8_t g = pow(mid, 3.0f) * 255;
            fxColor(i, 0, g, 0, 255);
        } 
        else {
            uint8_t b = pow(high, 3.0f) * 255;
            fxColor(i, 0, 0, b, 255);
        }
    }
}





// --------------------------------------------------
// Zeitschritt seit dem letzten Aufruf (je Effekt und Slot) für weiches Abklingen
// --------------------------------------------------
static float frameDt(unsigned long &last) {
    unsigned long now = millis();
    float dt = last ? (now - last) / 1000.0f : 0.02f;
    last = now;
    return constrain(dt, 0.0f, 0.2f);
}

// Grundfarbe für die Audio-Effekte: Kontextfarbe bzw. Palette (pro Auslöser weiter)
static void audioColor(int &palIdx, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = ctx.r; g = ctx.g; b = ctx.b;
    if (state.palette.on) {
        int n = constrain((int)state.palette.size, 2, PALETTE_MAX);
        const uint8_t *c = state.palette.c[((palIdx % n) + n) % n];
        r = c[0]; g = c[1]; b = c[2];
    }
}

// --------------------------------------------------
// PUMP: alle Lampen leuchten auf jedem Kick (bzw. Takt-Schritt) hell auf und klingen ab
// Intensität = wie lange das Nachleuchten dauert
// --------------------------------------------------
void effectPump() {
    static float level[GROUP_COUNT + 1];
    static unsigned long last[GROUP_COUNT + 1];
    static int palIdx[GROUP_COUNT + 1];
    int s = ctx.slot;

    float dt = frameDt(last[s]);
    if (colorChangeDue(2)) { level[s] = 1.0f; palIdx[s]++; }
    float tau = 0.08f + 0.25f * fxK();          // Abklingzeit in s (~0,33 s bei 50)
    level[s] *= expf(-dt / tau);

    uint8_t r, g, b;
    audioColor(palIdx[s], r, g, b);
    float v = level[s];
    for (int i = 0; i < fxCount(); i++) fxColor(i, r * v, g * v, b * v, 255);
}

// --------------------------------------------------
// SNARE-BLITZ: Grundfarbe gedimmt, auf jeder Snare ein weißer Blitz
// Intensität = Helligkeit der Grundfarbe zwischen den Blitzen
// --------------------------------------------------
void effectSnareFlash() {
    static uint32_t lastSnare[GROUP_COUNT + 1];
    static float flash[GROUP_COUNT + 1];
    static unsigned long last[GROUP_COUNT + 1];
    static int palIdx[GROUP_COUNT + 1];
    int s = ctx.slot;

    float dt = frameDt(last[s]);
    uint32_t sc = state.snareCount;
    if (sc != lastSnare[s]) { lastSnare[s] = sc; flash[s] = 1.0f; }
    flash[s] *= expf(-dt / 0.12f);

    uint8_t r, g, b;
    audioColor(palIdx[s], r, g, b);
    float base = constrain(0.25f * fxK(), 0.0f, 0.6f);
    float f = flash[s];
    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, r * base + (255 - r * base) * f,
                   g * base + (255 - g * base) * f,
                   b * base + (255 - b * base) * f, 255);
    }
}

// --------------------------------------------------
// BASS-WELLE: die Bass-Lautstärke läuft als Welle von der ersten zur letzten Lampe
// Intensität = wie schnell die Welle wandert (höher = langsamer, mehr Abstand)
// --------------------------------------------------
void effectBassWave() {
    static float hist[128];
    static int head = 0;
    static unsigned long lastPush = 0;
    static float smooth = 0;
    static int palIdx = 0;

    // Einmal pro ~20 ms einen neuen Bass-Wert aufnehmen (gilt für alle Slots)
    unsigned long now = millis();
    if (now - lastPush >= 20) {
        lastPush = now;
        float bass = constrain(state.kickEnergy / max(1.0, state.refKick), 0.0, 1.0);
        smooth = max(bass, smooth * 0.85f);   // schnell hoch, langsam runter
        head = (head + 1) % 128;
        hist[head] = smooth;
    }

    int delay = constrain((int)(1 + 2 * fxK()), 1, 8);   // Bilder Verzögerung pro Lampe
    for (int i = 0; i < fxCount(); i++) {
        int idx = ((head - i * delay) % 128 + 128) % 128;
        float v = hist[idx];
        uint8_t r, g, b;
        if (state.palette.on) paletteAt((float)i * state.palette.size / max(1, fxCount()), r, g, b);
        else audioColor(palIdx, r, g, b);
        fxColor(i, r * v, g * v, b * v, 255);
    }
}

// --------------------------------------------------
// EIGENES LAUFLICHT (user1 … user4): Schritte aus dem Editor, im Effekt-Takt
// (Speed-Regler bzw. "Im Takt"); optional weich zwischen den Schritten
// --------------------------------------------------
static void effectUserChase(int slot) {
    if (slot < 0 || slot >= USER_CHASES) return;
    const UserChase &uc = state.userChases[slot];
    if (uc.name.isEmpty() || uc.steps == 0 || uc.width == 0) return;

    double pos = stepPos();
    int step = ((long)pos) % uc.steps;
    int next = (step + 1) % uc.steps;
    float f = uc.smooth ? (float)(pos - floor(pos)) : 0.0f;

    for (int i = 0; i < fxCount(); i++) {
        int x = i % uc.width;
        uint32_t a = uc.cells[step][x], b = uc.cells[next][x];
        uint8_t r = ((a >> 16) & 0xFF) + ((int)((b >> 16) & 0xFF) - (int)((a >> 16) & 0xFF)) * f;
        uint8_t g = ((a >> 8) & 0xFF)  + ((int)((b >> 8) & 0xFF)  - (int)((a >> 8) & 0xFF))  * f;
        uint8_t bl = (a & 0xFF)        + ((int)(b & 0xFF)         - (int)(a & 0xFF))         * f;
        fxColor(i, r, g, bl, 255);
    }
}

// --------------------------------------------------
// DROP-REAKTION (überschreibt den Effekt für DROP_HOLD_MS)
// --------------------------------------------------
void effectDropReaction(uint8_t action, unsigned long elapsed) {
    useAllFixtures();
    int n = fxCount();

    if (action == DROP_FLASH) {
        // Weißer Blitz, der über die Drop-Dauer ausblendet
        float k = 1.0f - min(1.0f, elapsed / (float)DROP_HOLD_MS);
        uint8_t v = k * k * 255; // quadratisch → kräftiger Anfang, weiches Ende
        for (int i = 0; i < n; i++) fxColor(i, v, v, v, 255);
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
        for (int i = 0; i < n; i++) fxColor(i, palette[idx[i]][0], palette[idx[i]][1], palette[idx[i]][2], 255);
        return;
    }

    // DROP_STROBE (Standard): alle Lampen synchron weiß/aus, alle 40 ms Wechsel
    uint8_t s = ((millis() / 40) % 2 == 0) ? 255 : 0;
    for (int i = 0; i < n; i++) fxColor(i, s, s, s, 255);
}

// --------------------------------------------------
// MANUELLER STROBE (Knopf in der Web-UI)
// --------------------------------------------------
void effectManualStrobe() {
    useAllFixtures();
    // Feste ~12 Hz, unabhängig vom Effekt-Speed (wie der Drop-Strobe)
    uint8_t s = ((millis() / 40) % 2 == 0) ? 255 : 0;
    for (int i = 0; i < fxCount(); i++) {
        fxColor(i, s, s, s, 255);
    }
}

// --------------------------------------------------
// RUN EFFECT (Zentrale Steuerung)
// --------------------------------------------------
void runEffect(const String& name) {
    useAllFixtures();
    runEffectNamed(name);
}

void runEffectOn(const String& name, const std::vector<int>& fixtures,
                 uint8_t r, uint8_t g, uint8_t b, int slot) {
    ctx = {&fixtures, r, g, b, slot};
    runEffectNamed(name);
}

static void runEffectNamed(const String& name) {
    // Ohne Fixtures nichts tun (verhindert Division durch 0 in Chase-Effekten)
    if (fxCount() == 0) return;

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
    else if (name == "lightorgan" || name == "retro") effectLightOrgan(); // "retro" = alter Name
    else if (name == "pump") effectPump();
    else if (name == "snareflash") effectSnareFlash();
    else if (name == "basswave") effectBassWave();
    else if (name.startsWith("user")) effectUserChase(name.substring(4).toInt() - 1);
}
