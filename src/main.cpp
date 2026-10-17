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

// 1. Extern bekannt machen
extern void taskAudio(void *pv); 

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
    bool on = state.autoShow && state.presets.size() >= 2;
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

// Ein Bild der normalen Effekt-Steuerung berechnen und senden.
// Gibt die Wartezeit bis zum nächsten Bild zurück. Aufruf mit gehaltenem StateLock.
static int runEffectFrame(bool dropReaction, bool &wasStrobing) {
    // --- EFFEKT-LOGIK ---
    const String &fx = state.currentEffect;
    bool hasEffect = (fx != "none" && fx != "");
    bool strobing = (long)(state.manualStrobeUntil - millis()) > 0;

    if (strobing) {
        // Strobe-Knopf gedrückt → überschreibt den Effekt (hart, ohne Übergang)
        cancelFade();
        effectManualStrobe();
        updateDMX();
    } else if (dropReaction) {
        // Drop erkannt → eingestellte Reaktion überschreibt den Effekt
        cancelFade();
        effectDropReaction(state.dropAction, millis() - state.dropStartMs);
        updateDMX();
    } else if (hasEffect) {
        runEffect(fx);
        updateDMX();
    } else if (wasStrobing) {
        // Ohne Effekt nach dem Strobe dunkel schalten statt auf Weiß stehen zu bleiben
        for (int i = 0; i < (int)state.fixtures.size(); i++) setFixtureColor(i, 0, 0, 0, 0);
        updateDMX();
    }
    wasStrobing = strobing;

    // Dynamisches Delay
    bool audioFx = (fx == "disco" || fx == "disco2" || fx == "retro");
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

            // DMX-Schnittstelle neu starten, wenn die Web-UI die Config geändert hat
            if (state.dmxReinit) {
                state.dmxReinit = false;
                setupDMX();
            }

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

            // --- AUTO-SHOW ---
            handleAutoShow();

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
        }
        vTaskDelay(pdMS_TO_TICKS(max(10, delayTime)));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    
    if (!LittleFS.begin(true)) Serial.println("LittleFS Error!");

    initState(); // Lädt beim Start alle gespeicherten Werte
    updateDMXChannelCount();
    setupDMX();
    setupWiFi(); 
    setupWeb(); 
    setupNetInput(); // Art-Net / sACN, falls eingeschaltet

    // Audio Task auf CORE 0
    xTaskCreatePinnedToCore(taskAudio, "AudioTask", 10240, NULL, 2, NULL, 0);

    // Effekt Task auf CORE 1 (beinhaltet nun auch AutoSave)
    xTaskCreatePinnedToCore(effectTask, "effectTask", 6144, NULL, 1, NULL, 1);

    Serial.println("System ready.");
}

void loop() {
    // Effekte und Audio laufen in eigenen FreeRTOS-Tasks;
    // die Loop kümmert sich um WLAN, OTA-Updates und geplante Neustarts
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
