#include <Arduino.h>
#include <LittleFS.h>
#include "state.h"
#include "dmx.h"
#include "audio.h"
#include "web.h"
#include "effects.h"
#include "wifi.h"
#include "ota.h"
#include "netinput.h"
#include "display.h"


// Geplanter Neustart (z. B. nach dem Speichern der WLAN-Daten)
static volatile unsigned long restartAt = 0;

void scheduleRestart(unsigned long delayMs) {
    unsigned long t = millis() + delayMs;
    restartAt = t ? t : 1;
}

// Auto-Show: schaltet alle showBeats Beats zum nächsten Preset.
// Ohne erkannte Beats wird mit 120 BPM gerechnet (0,5 s pro Beat).
// Aufruf im Effekt-Task mit gehaltenem StateLock.
static void handleAutoShow() {
    static bool wasOn = false;
    static uint32_t startBeat = 0;
    static unsigned long startMs = 0;
    static int index = -1;

    unsigned long now = millis();
    // Während Standby oder laufender Cue-Liste pausiert die Auto-Show
    bool on = state.autoShow && state.presets.size() >= 2 && !state.standbyActive && !state.cueRunning;
    if (!on || !wasOn) {
        // (Neu-)Start: ab jetzt zählen
        startBeat = state.beatCount;
        startMs = now;
        wasOn = on;
        return;
    }

    bool due = (state.bpm >= 30)
        ? (state.beatCount - startBeat >= (uint32_t)state.showBeats)
        : (now - startMs >= (unsigned long)state.showBeats * 500UL);
    if (!due) return;

    index = (index + 1) % state.presets.size();
    applyPreset(index, false); // nicht speichern → schont den Flash
    startBeat = state.beatCount;
    startMs = now;
}

// Cue-Liste: Presets in fester Reihenfolge, jeder Cue mit eigener Dauer
// (Beats oder Sekunden; ohne erkannte Beats zählt ein Beat 0,5 s).
static void handleCues() {
    static uint32_t startBeat = 0;
    static unsigned long startMs = 0;

    if (!state.cueRunning || state.cues.empty()) {
        state.cueRunning = false;
        state.cueIndex = -1;
        return;
    }

    unsigned long now = millis();
    int next = -1;
    if (state.cueRestart) {
        state.cueRestart = false;
        next = 0;
    } else if (state.cueNext) {
        state.cueNext = false;
        next = state.cueIndex + 1;
    } else if (!state.standbyActive && state.cueIndex >= 0 && state.cueIndex < (int)state.cues.size()) {
        const Cue &c = state.cues[state.cueIndex];
        bool due = c.beats
            ? (state.bpm >= 30 ? state.beatCount - startBeat >= (uint32_t)c.duration
                               : now - startMs >= (unsigned long)c.duration * 500UL)
            : now - startMs >= (unsigned long)c.duration * 1000UL;
        if (due) next = state.cueIndex + 1;
    }
    if (next < 0) return;

    if (next >= (int)state.cues.size()) {
        if (!state.cueLoop) {
            state.cueRunning = false;
            state.cueIndex = -1;
            return;
        }
        next = 0;
    }
    state.cueIndex = next;
    applyPreset(findPreset(state.cues[next].preset), false);
    startBeat = state.beatCount;
    startMs = now;
}

// Standby: Läuft standbyMin Minuten keine Musik (keine Kicks aus dem Mikrofon),
// wechselt der Controller auf das Standby-Preset bzw. dimmt auf 20 %.
// Kommen wieder 4 Kicks innerhalb von 4 s, wird der vorherige Zustand wiederhergestellt.
// Bedient jemand die UI während Standby, bleibt dessen Einstellung (kein Zurückspringen).
static void handleStandby() {
    static Preset before;
    static uint32_t lastKickCount = 0;
    static unsigned long kickTimes[4] = {0};
    static int kickPos = 0;
    static int kicksSinceStandby = 0;
    static int dimmedMaster = -1;   // Master im "nur dimmen"-Standby, sonst -1

    unsigned long now = millis();
    uint32_t kc = state.audioKickCount;
    if (kc != lastKickCount) {
        lastKickCount = kc;
        kickTimes[kickPos] = now;
        kickPos = (kickPos + 1) % 4;
        kicksSinceStandby++;
    }

    if (state.userTouched) {
        state.userTouched = false;
        if (state.standbyActive) {
            // Bedienung gewinnt, kein Wiederherstellen – nur das Dimmen zurücknehmen,
            // falls nicht gerade der Master selbst verstellt wurde
            if (dimmedMaster >= 0 && state.master == dimmedMaster) state.master = before.master;
            state.standbyActive = false;
        }
    }

    bool enabled = state.standbyMin > 0 && !netInputActive();
    if (!state.standbyActive) {
        if (!enabled) return;
        if (now - state.lastAudioKickMs < (unsigned long)state.standbyMin * 60000UL) return;

        before = capturePreset("");
        int p = findPreset(state.standbyPreset);
        if (p >= 0) {
            applyPreset(p, false);
        } else {
            state.master = state.master * 20 / 100; // nur dimmen
            state.fadeRequest = true;
        }
        dimmedMaster = (p >= 0) ? -1 : state.master;
        state.standbyActive = true;
        kicksSinceStandby = 0;
        Serial.println("Standby: keine Musik → ruhige Szene");
        return;
    }

    // Aufwachen: 4 Kicks innerhalb von 4 s (oder Standby abgeschaltet)
    unsigned long oldest = kickTimes[kickPos]; // ältester der letzten 4
    bool music = kicksSinceStandby >= 4 && now - oldest < 4000;
    if (music || !enabled) {
        applyPresetData(before, false);
        state.standbyActive = false;
        Serial.println("Standby beendet: Musik erkannt");
    }
}

// Nebel: automatische Stöße bei Drops und im Intervall (Ausgabe in updateDMX)
static void handleFog(bool newDrop) {
    static unsigned long lastInterval = 0;
    if (state.fogChannel <= 0) return;

    unsigned long now = millis();
    if (newDrop && state.fogOnDrop) {
        unsigned long until = now + state.fogDropSec * 1000UL;
        if ((long)(until - state.fogUntil) > 0) state.fogUntil = until;
    }
    if (state.fogIntervalMin > 0) {
        if (now - lastInterval >= (unsigned long)state.fogIntervalMin * 60000UL) {
            lastInterval = now;
            state.fogUntil = now + state.fogIntervalSec * 1000UL;
        }
    } else {
        lastInterval = now; // nach dem Einschalten erst ein volles Intervall warten
    }
}

static bool isAudioEffect(const String &fx) {
    return fx == "disco" || fx == "disco2" || fx == "lightorgan" || fx == "retro";
}

// Ein Bild der normalen Effekt-Steuerung berechnen und senden.
// Gibt die Wartezeit bis zum nächsten Bild zurück. Aufruf mit gehaltenem StateLock.
static int runEffectFrame(bool dropReaction, bool &wasStrobing) {
    // Fixtures aufteilen: Haupteffekt (Slot 0) und Gruppen mit eigenem Effekt (Slot 1…4)
    static std::vector<int> sets[GROUP_COUNT + 1];
    for (auto &v : sets) v.clear();
    for (int i = 0; i < (int)state.fixtures.size(); i++) {
        uint8_t g = state.fixtures[i].group;
        bool own = g >= 1 && g <= GROUP_COUNT && state.groups[g - 1].custom;
        sets[own ? g : 0].push_back(i);
    }

    // --- EFFEKT-LOGIK ---
    const String &fx = state.currentEffect;
    bool hasEffect = (fx != "none" && fx != "");
    bool strobing = (long)(state.manualStrobeUntil - millis()) > 0;
    bool audioFx = hasEffect && isAudioEffect(fx);

    if (strobing) {
        // Strobe-Knopf gedrückt → überschreibt den Effekt (hart, ohne Übergang)
        cancelFade();
        effectManualStrobe();
    } else if (dropReaction) {
        // Drop erkannt → eingestellte Reaktion überschreibt den Effekt
        cancelFade();
        effectDropReaction(state.dropAction, millis() - state.dropStartMs);
    } else {
        if (hasEffect) runEffectOn(fx, sets[0], state.colorR, state.colorG, state.colorB, 0);
        for (int g = 1; g <= GROUP_COUNT; g++) {
            const GroupSettings &G = state.groups[g - 1];
            if (sets[g].empty() || G.effect == "none") continue;
            runEffectOn(G.effect, sets[g], G.r, G.g, G.b, g);
            audioFx |= isAudioEffect(G.effect);
        }
        if (!hasEffect && wasStrobing) {
            // Ohne Effekt nach dem Strobe dunkel schalten statt auf Weiß stehen zu bleiben
            for (int i : sets[0]) setFixtureColor(i, 0, 0, 0, 0);
        }
    }
    wasStrobing = strobing;

    // Immer senden: DMX-Geräte erwarten laufend Daten (und der Nebelkanal muss raus)
    updateDMX();

    // Dynamisches Delay
    // Während eines Übergangs mit ~50 fps senden, damit er flüssig aussieht
    // Im Takt ebenfalls 20 ms, damit die Schritte genau auf dem Beat liegen
    bool fast = audioFx || isFading() || state.beatSync || dropReaction;
    return strobing ? 10 : fast ? 20 : (state.timerSpeed / 20);
}

// 2. Effekt Task (Core 1)
void effectTask(void *param) {
    uint32_t lastBeatCount = state.beatCount;
    uint32_t lastDropCount = state.dropCount;
    bool wasStrobing = false;
    bool wasNet = false;
    static uint8_t netFrame[512];

    while (true) {
        int delayTime;
        {
            StateLock lock;

            // --- SPEICHER-CHECK ---
            // Prüft alle paar Millisekunden, ob seit 5 Sek. nichts mehr am Webinterface 
            // gedreht wurde und speichert dann einmalig den Flash-Speicher.
            if (!state.otaActive) handleAutoSave(); 

            uint32_t t0 = micros(); // Rechenzeit des Bildes messen

            // --- BEAT ---
            // Der Audio-Task zählt Kicks hoch; so geht kein Beat verloren
            // und keiner wird doppelt ausgewertet
            uint32_t beats = state.beatCount;
            state.beat = (beats != lastBeatCount);
            lastBeatCount = beats;

            // --- DROP ---
            uint32_t drops = state.dropCount;
            bool newDrop = (drops != lastDropCount);
            lastDropCount = drops;
            if (newDrop && state.dropAction == DROP_PRESET) {
                applyPreset(findPreset(state.dropPreset), false);
            }
            bool dropReaction = state.drop && state.dropAction != DROP_PRESET &&
                                millis() - state.dropStartMs < DROP_HOLD_MS;

            // --- STANDBY, CUE-LISTE, AUTO-SHOW, NEBEL ---
            handleStandby();
            handleCues();
            handleAutoShow();
            handleFog(newDrop);

            // --- PC-STEUERUNG (Art-Net / sACN) ---
            // Kommen Netzwerk-Daten, werden sie 1:1 ausgegeben; Effekte pausieren
            if (netInputActive()) {
                updateDMXFromNetwork(netFrame, copyNetDmx(netFrame));
                wasNet = true;
                delayTime = 20;
            } else {
                if (wasNet) {
                    wasNet = false;
                    state.fadeRequest = true; // weich zurück zur Effekt-Steuerung
                }
                delayTime = runEffectFrame(dropReaction, wasStrobing);
            }
            recordLoad(loadStats.effectUs, loadStats.effectMaxUs, micros() - t0);
        }
        vTaskDelay(pdMS_TO_TICKS(max(10, delayTime)));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    
    setupDisplay(); // zeigt "startet …", während das WLAN verbindet
    if (!LittleFS.begin(true)) Serial.println("LittleFS Error!");

    initState(); // Lädt beim Start alle gespeicherten Werte
    updateDMXChannelCount();
    setupDMX();
    setupWiFi(); 
    setupWeb(); 
    setupNetInput(); // Art-Net / sACN, falls eingeschaltet

    // Core 0: Audio (+ WLAN-Stack, Display)
    xTaskCreatePinnedToCore(taskAudio, "AudioTask", 10240, NULL, PRIO_AUDIO, &loadStats.audioTask, CORE_AUDIO);
    startDisplayTask();

    // Core 1: Effekte (+ DMX-Senden, Webserver, loop). Beinhaltet auch das AutoSave.
    xTaskCreatePinnedToCore(effectTask, "effectTask", 6144, NULL, PRIO_EFFECT, &loadStats.effectTask, CORE_LIGHT);

    Serial.println("System ready.");
}

void loop() {
    // Effekte und Audio laufen in eigenen FreeRTOS-Tasks;
    // die Loop (Core 1) kümmert sich um WLAN, OTA-Updates, WebSocket und geplante Neustarts
    handleWiFi();
    handleOTA();
    handleLiveStatus();

    if (restartAt && (long)(millis() - restartAt) >= 0) {
        StateLock lock; // wartet, bis ein laufender Speichervorgang fertig ist
        Serial.println("Neustart …");
        delay(100);
        ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(10)); 
}
