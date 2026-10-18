#include <SparkFunDMX.h>
#include "dmx.h"
#include "state.h"
#include "effects.h"
#include <vector>

extern ControllerState state;

// UART2
HardwareSerial dmxSerial(2);

uint8_t txPin = 32;
uint8_t rxPin = 16;
uint8_t enPin = 33;

SparkFunDMX dmx;

// ------------------------------------------------------------
// Aufteilung: Der Effekt-Task berechnet das Bild (sentOut) und übergibt es;
// ein eigener DMX-Task (Core 1, höchste Priorität) sendet es. So blockiert
// das langsame serielle Senden (bis ~23 ms bei 512 Kanälen) weder die Effekte
// noch – über den StateLock – den Webserver.
// ------------------------------------------------------------
#define DMX_REFRESH_MS 100            // ohne neues Bild spätestens so oft wiederholen

static uint8_t sentOut[512];          // Zuletzt berechnetes Bild (auch für den DMX-Monitor)
static int sentLen = 0;

static portMUX_TYPE frameMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t pendingFrame[512];     // Übergabe an den DMX-Task
static int pendingLen = 0;
static TaskHandle_t dmxTaskHandle = nullptr;

static int outChannels = 0;           // Nur im DMX-Task: so viele Kanäle sendet die Schnittstelle

// Bild an den DMX-Task übergeben (aus dem Effekt-Task)
static void publishFrame(int len) {
    sentLen = len;
    portENTER_CRITICAL(&frameMux);
    memcpy(pendingFrame, sentOut, len);
    pendingLen = len;
    portEXIT_CRITICAL(&frameMux);
    if (dmxTaskHandle) xTaskNotifyGive(dmxTaskHandle);
}

// Nur im DMX-Task: Anzahl der gesendeten Kanäle anpassen (z. B. wenn ein PC per
// Art-Net mehr Kanäle steuert, als Fixtures angelegt sind). Nur bei Änderung.
static void ensureOutputChannels(int n) {
    n = constrain(n, 1, 512);
    if (n == outChannels) return;
    dmx.begin(dmxSerial, enPin, n);
    dmx.setComDir(DMX_WRITE_DIR);
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

static void startDMXHardware() {
    // 1. Enable-Pin explizit als Ausgang definieren
    pinMode(enPin, OUTPUT);
    
    // 2. Pin manuell auf HIGH setzen, um den RS485-Treiber auf "Senden" zu schalten
    digitalWrite(enPin, HIGH);
    if (state.numChannels > 512) state.numChannels = 512;
    dmxSerial.begin(DMX_BAUD, DMX_FORMAT, rxPin, txPin);
    dmx.begin(dmxSerial, enPin, max(1, state.numChannels));
    dmx.setComDir(DMX_WRITE_DIR);
    outChannels = max(1, state.numChannels);
    Serial.printf("DMX gestartet mit %d Kanälen\n", state.numChannels);
}

static void dmxTask(void *param) {
    static uint8_t frame[512];
    for (;;) {
        // Auf ein neues Bild warten – oder nach DMX_REFRESH_MS das letzte wiederholen
        // (DMX-Geräte erwarten laufend Daten)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DMX_REFRESH_MS));

        if (state.dmxReinit) {   // Fixture-Config geändert
            state.dmxReinit = false;
            dmxSerial.flush();
            startDMXHardware();
        }

        int len;
        portENTER_CRITICAL(&frameMux);
        len = pendingLen;
        memcpy(frame, pendingFrame, len);
        portEXIT_CRITICAL(&frameMux);

        uint32_t t0 = micros();
        // Erst warten, bis das vorige Bild komplett draußen ist: Die Bibliothek
        // schaltet für das Break die Baudrate um und würde sonst dessen Ende verstümmeln
        dmxSerial.flush();
        ensureOutputChannels(len);
        if (len > 0) dmx.writeBytes(frame, len, 1);
        dmx.update();
        recordLoad(loadStats.dmxUs, loadStats.dmxMaxUs, micros() - t0);
        loadStats.dmxFrames++;
    }
}

void setupDMX() {
    if (!dmxTaskHandle) {
        startDMXHardware();
        xTaskCreatePinnedToCore(dmxTask, "dmxTask", 4096, NULL, PRIO_DMX, &dmxTaskHandle, CORE_LIGHT);
        loadStats.dmxTask = dmxTaskHandle;
    } else {
        state.dmxReinit = true;   // Neustart übernimmt der DMX-Task selbst
        xTaskNotifyGive(dmxTaskHandle);
    }
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
    // 1. Prüfung: Haben wir einen Takt?
    if (state.bpm < 30) return; 

    // --- BEAT TRIGGER LOGIK ---
    // Auslöser: bei "Im Takt" jeder Effekt-Schritt (½ / 1 / 2 Beats), sonst jeder Beat
    static long lastStep = -1;
    bool trigger = state.beat;
    if (state.beatSync) {
        long step = effectStepIndex();
        trigger = (step != lastStep);
        lastStep = step;
    }

    // Kreis: Richtung umkehren + Phasensprung
    if (trigger) {
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
    if (state.movePattern == MOVE_RANDOM && trigger) {
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

        // 2. Prüfung: Nur bei Disco-Effekten bewegen (Haupt- oder Gruppen-Effekt des Heads)
        const String &eff = effectiveEffect(i);
        if (eff != "disco" && eff != "disco2") continue;

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

    // Nebelmaschine kann hinter den Fixtures liegen → so weit ausgeben
    int fogIdx = state.fogChannel - 1;          // -1 = keine
    int total = constrain(max(state.numChannels, state.fogChannel), 1, 512);

    unsigned long now = millis();
    static int fadeDuration = 600;
    if (state.fadeRequest) {
        state.fadeRequest = false;
        // Preset kann eine eigene Übergangszeit mitbringen, sonst die globale
        fadeDuration = state.nextFadeMs >= 0 ? state.nextFadeMs : state.fadeMs;
        state.nextFadeMs = -1;
        memcpy(fadeFrom, lastOut, sizeof(fadeFrom));
        fadeStart = now;
        fading = fadeDuration > 0;
    }
    float t = 1.0f;
    if (fading) {
        t = (now - fadeStart) / (float)fadeDuration;
        if (t >= 1.0f) { t = 1.0f; fading = false; }
    }

    // Helligkeit folgt der Lautstärke: schnell heller, langsam dunkler
    static float loud = 1.0f;
    float lvlFactor = 1.0f;
    if (state.levelDim) {
        float lvl = constrain(state.level / max(1.0, state.refLevel) * 1.5, 0.0, 1.0);
        loud += (lvl - loud) * (lvl > loud ? 0.5f : 0.05f);
        float depth = state.levelDimDepth / 100.0f;
        lvlFactor = 1.0f - depth + depth * loud;
    }

    // Blackout bzw. Zeitplan "aus": Helligkeit sofort auf 0 (der Strobe-Knopf hat Vorrang)
    bool strobing = (long)(state.manualStrobeUntil - now) > 0;
    bool scheduleDark = state.scheduleDark && !state.scheduleOverride;
    bool dark = (state.blackout || scheduleDark) && !strobing;

    unsigned long nowFog = millis();
    bool fogOn = fogIdx >= 0 && ((long)(state.fogUntil - nowFog) > 0 || (long)(state.manualFogUntil - nowFog) > 0);

    for (int i = 0; i < total; i++) {
        uint8_t v = i < state.numChannels ? state.dmx[i] : 0;
        if (v == 0 && i < state.numChannels) v = state.dmxPreview[i];

        // WICHTIG: Master nur auf Helligkeit anwenden, NICHT auf Pan/Tilt
        bool dimmable = isDimmableChannel(i);
        if (dimmable) {
            v = applyMaster(v);
            if (state.levelDim) v = v * lvlFactor;
        }

        if (fading) v = fadeFrom[i] + (int)(((int)v - (int)fadeFrom[i]) * t);
        lastOut[i] = v;

        uint8_t out = (dark && dimmable) ? 0 : v;
        // Nebel: eigener Kanal, unabhängig von Master, Übergängen und Blackout
        if (i == fogIdx) out = fogOn ? state.fogLevel : 0;
        sentOut[i] = out;
    }
    publishFrame(total);
}

void updateDMXFromNetwork(const uint8_t *data, int len) {
    len = constrain(len, 0, 512);
    int total = constrain(max(len, state.numChannels), 1, 512);
    cancelFade();

    // Nebel-Knopf/-Automatik wirkt auch bei PC-Steuerung (überschreibt nur, solange aktiv)
    unsigned long now = millis();
    int fogIdx = state.fogChannel - 1;
    bool fogOn = fogIdx >= 0 && ((long)(state.fogUntil - now) > 0 || (long)(state.manualFogUntil - now) > 0);
    if (fogOn) total = max(total, (int)state.fogChannel);

    bool dark = state.blackout;
    for (int i = 0; i < total; i++) {
        uint8_t v = i < len ? data[i] : 0;
        lastOut[i] = v; // damit nach dem Art-Net-Betrieb sauber übergeblendet wird
        uint8_t out = (dark && isDimmableChannel(i)) ? 0 : v;
        if (fogOn && i == fogIdx) out = state.fogLevel;
        sentOut[i] = out;
    }
    publishFrame(total);
}

int getDMXOutput(uint8_t *buf, int maxLen) {
    int n = min(sentLen, maxLen);
    memcpy(buf, sentOut, n);
    return n;
}
