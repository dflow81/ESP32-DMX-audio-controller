#include <SparkFunDMX.h>
#include "dmx.h"
#include "state.h"

extern ControllerState state;

// UART2
HardwareSerial dmxSerial(2);

uint8_t txPin = 32;
uint8_t rxPin = 16;
uint8_t enPin = 33;

SparkFunDMX dmx;

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
    Serial.printf("DMX gestartet mit %d Kanälen\n", state.numChannels);
}

// MH
// Statische Variablen für Beat-Effekte
static float beatDirection = 1.0f;
static float beatOffset = 0.0f;

void updateMovements() {
    // 1. Prüfung: Nur bei Disco-Effekten bewegen
    bool isDiscoEffect = (state.currentEffect == "disco" || state.currentEffect == "disco2");
    if (!isDiscoEffect) return;

    // 2. Prüfung: Haben wir einen Takt?
    if (state.bpm < 30) return; 

    // --- BEAT TRIGGER LOGIK ---
    // Wenn ein Beat erkannt wurde, ändern wir die Parameter
    if (state.beat) {
        beatDirection *= -1.0f;          // Richtung umkehren (1.0 oder -1.0)
        beatOffset += random(10, 100) / 10.0f; // Zufälliger Phasensprung
    }

    float bps = state.bpm / 60.0f;
    // Wir multiplizieren die Zeit mit der beatDirection
    float globalPhase = (millis() / 1000.0f) * bps * TWO_PI * beatDirection;

    for (int i = 0; i < (int)state.fixtures.size(); i++) {
        auto &fx = state.fixtures[i];
        
        // Nur Heads bewegen, für die "BPM Auto" eingeschaltet ist;
        // die anderen bleiben dort, wo der Joystick sie hingestellt hat
        if (!fx.isAutoMoving) continue;

        const FixtureDefinition* def = getFixtureDef(fx.type);
        if (def && def->offsetPan >= 0) {
            
            // Lokale Phase mit individuellem Offset + globalem Beat-Offset
            float localPhase = globalPhase + (i * 0.8f) + beatOffset;

            // Pan/Tilt Berechnung
            float wavePan = (sin(localPhase) + 1.0f) / 2.0f; 
            float waveTilt = (cos(localPhase) + 1.0f) / 2.0f;

            // Skalierung auf Eckpunkte A und B
            uint8_t p = fx.panMin + (uint8_t)(wavePan * (fx.panMax - fx.panMin));
            uint8_t t = fx.tiltMin + (uint8_t)(waveTilt * (fx.tiltMax - fx.tiltMin));

            setFixtureMovement(i, p, t);
        }
    }
}




void updateDMX() {
    updateMovements(); // Vor dem Senden Bewegungen berechnen

    for (int i = 0; i < state.numChannels; i++) {
        uint8_t v = state.dmx[i];
        if (v == 0) v = state.dmxPreview[i];

        // WICHTIG: Master nur auf Helligkeit anwenden, NICHT auf Pan/Tilt
        if (isDimmableChannel(i)) {
            v = applyMaster(v);
        }

        dmx.writeByte(v, i + 1);
    }
    dmx.update();
}
