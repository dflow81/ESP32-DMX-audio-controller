#include <SparkFunDMX.h>
#include "dmx.h"
#include "state.h"
#include <vector>

extern ControllerState state;

// UART2
HardwareSerial dmxSerial(2);

uint8_t txPin = 32;
uint8_t rxPin = 16;
uint8_t enPin = 33;

SparkFunDMX dmx;

static int outChannels = 0;        // So viele Kanäle sendet die Schnittstelle gerade
static uint8_t sentOut[512];       // Zuletzt gesendete Werte (für den DMX-Monitor)

// Passt die Anzahl der gesendeten Kanäle an (z. B. wenn ein PC per Art-Net
// mehr Kanäle steuert, als Fixtures angelegt sind). Nur bei Änderung.
static void ensureOutputChannels(int n) {
    n = constrain(n, 1, 512);
    if (n == outChannels) return;
    dmx.begin(dmxSerial, enPin, n);
    dmx.setComDir(DMX_WRITE_DIR);
    if (n < outChannels) memset(sentOut + n, 0, outChannels - n);
    outChannels = n;
}

// Hilfsfunktion: Prüft, ob ein Kanal gedimmt werden darf
// (Maske wird in updateDMXChannelCount() vorberechnet)
bool isDimmableChannel(int channelIndex) {
    if (channelIndex < 0 || channelIndex >= 512) return false;
    return state.dimmable[channelIndex];
}

// Schreibt einen Kanal nur, wenn Offset gültig ist und im DMX-Puffer liegt
static inline void setChannel(int base, int offset, uint8_t v) {
    if (offset < 0) return;
    int ch = base + offset;
    if (ch >= 0 && ch < 512) state.dmx[ch] = v;
}

// Master anwenden
uint8_t applyMaster(uint8_t v) {
    return (v * state.master) / 255;
}

// NEU: Bewegung setzen
void setFixtureMovement(int index, uint8_t pan, uint8_t tilt) {
    if (index < 0 || index >= (int)state.fixtures.size()) return;

    auto &fx = state.fixtures[index];
    const FixtureDefinition* def = getFixtureDef(fx.type);
    if (!def) return;

    int base = fx.start - 1;

    setChannel(base, def->offsetPan, pan);
    setChannel(base, def->offsetTilt, tilt);
}

void setFixtureColor(int index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (index < 0 || index >= (int)state.fixtures.size()) return;

    auto &fx = state.fixtures[index];
    const FixtureDefinition* def = getFixtureDef(fx.type);
    if (!def) return;

    int base = fx.start - 1;

    setChannel(base, def->offsetDimmer, 255);
    setChannel(base, def->offsetR, r);
    setChannel(base, def->offsetG, g);
    setChannel(base, def->offsetB, b);
    setChannel(base, def->offsetW, w);
}

void setupDMX() {
    // 1. Enable-Pin explizit als Ausgang definieren
    pinMode(enPin, OUTPUT);
    
    // 2. Pin manuell auf HIGH setzen, um den RS485-Treiber auf "Senden" zu schalten
    digitalWrite(enPin, HIGH);
    if (state.numChannels > 512) state.numChannels = 512;
    dmxSerial.begin(DMX_BAUD, DMX_FORMAT, rxPin, txPin);
    dmx.begin(dmxSerial, enPin, state.numChannels);
    dmx.setComDir(DMX_WRITE_DIR);
    outChannels = max(1, state.numChannels);
    Serial.printf("DMX gestartet mit %d Kanälen\n", state.numChannels);
}

// MH
// Statische Variablen für Beat-Effekte
static float beatDirection = 1.0f;
static float beatOffset = 0.0f;

// Skaliert 0…1 auf den Bereich zwischen Punkt A und B (funktioniert auch, wenn A > B)
static uint8_t scaleToRange(float w, uint8_t a, uint8_t b) {
    return constrain(a + (int)lroundf(w * ((int)b - (int)a)), 0, 255);
}

void updateMovements() {
    // 1. Prüfung: Nur bei Disco-Effekten bewegen
    bool isDiscoEffect = (state.currentEffect == "disco" || state.currentEffect == "disco2");
    if (!isDiscoEffect) return;

    // 2. Prüfung: Haben wir einen Takt?
    if (state.bpm < 30) return; 

    // --- BEAT TRIGGER LOGIK (nur Kreis, wie bisher) ---
    // Wenn ein Beat erkannt wurde, ändern wir die Parameter
    if (state.beat) {
        beatDirection *= -1.0f;          // Richtung umkehren (1.0 oder -1.0)
        beatOffset += random(10, 100) / 10.0f; // Zufälliger Phasensprung
    }

    float bps = state.bpm / 60.0f;
    float seconds = millis() / 1000.0f;
    // Kreis: ein Umlauf pro Beat, Richtung springt auf jedem Beat
    float circlePhase = seconds * bps * TWO_PI * beatDirection;
    // Andere Muster: ein gleichmäßiger Durchlauf pro Takt (4 Beats)
    float barPhase = seconds * bps * TWO_PI / 4.0f;

    // Zufall: Zielpositionen pro Head, wechseln auf jedem Beat, weich angefahren
    static std::vector<float> randTargetP, randTargetT, randP, randT;
    int n = state.fixtures.size();
    if ((int)randP.size() < n) {
        randTargetP.resize(n, 0.5f); randTargetT.resize(n, 0.5f);
        randP.resize(n, 0.5f);       randT.resize(n, 0.5f);
    }
    if (state.movePattern == MOVE_RANDOM && state.beat) {
        float sharedP = random(0, 1001) / 1000.0f, sharedT = random(0, 1001) / 1000.0f;
        for (int i = 0; i < n; i++) {
            randTargetP[i] = state.moveSpread ? random(0, 1001) / 1000.0f : sharedP;
            randTargetT[i] = state.moveSpread ? random(0, 1001) / 1000.0f : sharedT;
        }
    }

    float spread = state.moveSpread ? 0.8f : 0.0f; // Versatz zwischen den Heads (Welle)

    for (int i = 0; i < n; i++) {
        auto &fx = state.fixtures[i];
        
        // Nur Heads bewegen, für die "BPM Auto" eingeschaltet ist;
        // die anderen bleiben dort, wo der Joystick sie hingestellt hat
        if (!fx.isAutoMoving) continue;

        const FixtureDefinition* def = getFixtureDef(fx.type);
        if (!def || def->offsetPan < 0) continue;

        float wavePan = 0.5f, waveTilt = 0.5f;
        float lp = barPhase + i * spread;

        switch (state.movePattern) {
            case MOVE_EIGHT:   // liegende Acht
                wavePan  = (sin(lp) + 1.0f) / 2.0f;
                waveTilt = (sin(2.0f * lp) + 1.0f) / 2.0f;
                break;
            case MOVE_PAN:     // Schwenk links/rechts
                wavePan  = (sin(lp) + 1.0f) / 2.0f;
                break;
            case MOVE_TILT:    // Nicken hoch/runter
                waveTilt = (sin(lp) + 1.0f) / 2.0f;
                break;
            case MOVE_RANDOM:
                randP[i] += (randTargetP[i] - randP[i]) * 0.15f;
                randT[i] += (randTargetT[i] - randT[i]) * 0.15f;
                wavePan  = randP[i];
                waveTilt = randT[i];
                break;
            default: {         // MOVE_CIRCLE
                float cp = circlePhase + i * spread + beatOffset;
                wavePan  = (sin(cp) + 1.0f) / 2.0f;
                waveTilt = (cos(cp) + 1.0f) / 2.0f;
                break;
            }
        }

        // Skalierung auf Eckpunkte A und B
        setFixtureMovement(i, scaleToRange(wavePan, fx.panMin, fx.panMax),
                              scaleToRange(waveTilt, fx.tiltMin, fx.tiltMax));
    }
}

// --- Weiche Übergänge ---
static uint8_t lastOut[512];   // Zuletzt gesendetes Bild (vor Blackout)
static uint8_t fadeFrom[512];  // Bild zu Beginn des Übergangs
static unsigned long fadeStart = 0;
static bool fading = false;

bool isFading() { return fading || state.fadeRequest; }

void cancelFade() {
    fading = false;
    state.fadeRequest = false;
}

void updateDMX() {
    updateMovements(); // Vor dem Senden Bewegungen berechnen
    ensureOutputChannels(state.numChannels); // nach Art-Net-Betrieb wieder normal

    unsigned long now = millis();
    if (state.fadeRequest) {
        state.fadeRequest = false;
        memcpy(fadeFrom, lastOut, sizeof(fadeFrom));
        fadeStart = now;
        fading = true;
    }
    float t = 1.0f;
    if (fading) {
        t = (now - fadeStart) / (float)FADE_MS;
        if (t >= 1.0f) { t = 1.0f; fading = false; }
    }

    // Blackout: Helligkeit sofort auf 0 (der Strobe-Knopf hat Vorrang)
    bool strobing = (long)(state.manualStrobeUntil - now) > 0;
    bool dark = state.blackout && !strobing;

    for (int i = 0; i < state.numChannels; i++) {
        uint8_t v = state.dmx[i];
        if (v == 0) v = state.dmxPreview[i];

        // WICHTIG: Master nur auf Helligkeit anwenden, NICHT auf Pan/Tilt
        bool dimmable = isDimmableChannel(i);
        if (dimmable) {
            v = applyMaster(v);
        }

        if (fading) v = fadeFrom[i] + (int)(((int)v - (int)fadeFrom[i]) * t);
        lastOut[i] = v;

        uint8_t out = (dark && dimmable) ? 0 : v;
        sentOut[i] = out;
        dmx.writeByte(out, i + 1);
    }
    dmx.update();
}

void updateDMXFromNetwork(const uint8_t *data, int len) {
    len = constrain(len, 0, 512);
    ensureOutputChannels(max(len, state.numChannels));
    cancelFade();

    bool dark = state.blackout;
    for (int i = 0; i < outChannels; i++) {
        uint8_t v = i < len ? data[i] : 0;
        lastOut[i] = v; // damit nach dem Art-Net-Betrieb sauber übergeblendet wird
        uint8_t out = (dark && isDimmableChannel(i)) ? 0 : v;
        sentOut[i] = out;
        dmx.writeByte(out, i + 1);
    }
    dmx.update();
}

int getDMXOutput(uint8_t *buf, int maxLen) {
    int n = min(outChannels, maxLen);
    memcpy(buf, sentOut, n);
    return n;
}
