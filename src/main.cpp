#include <Arduino.h>
#include <LittleFS.h>
#include "state.h"
#include "dmx.h"
#include "audio.h"
#include "web.h"
#include "effects.h"
#include "wifi.h"
#include "ota.h"

// 1. Extern bekannt machen
extern void taskAudio(void *pv); 

// Geplanter Neustart (z. B. nach dem Speichern der WLAN-Daten)
static volatile unsigned long restartAt = 0;

void scheduleRestart(unsigned long delayMs) {
    unsigned long t = millis() + delayMs;
    restartAt = t ? t : 1;
}

// 2. Effekt Task (Core 1)
void effectTask(void *param) {
    uint32_t lastBeatCount = state.beatCount;
    bool wasStrobing = false;

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

            // --- EFFEKT-LOGIK ---
            const String &fx = state.currentEffect;
            bool hasEffect = (fx != "none" && fx != "");
            bool strobing = (long)(state.manualStrobeUntil - millis()) > 0;

            if (strobing) {
                // Strobe-Knopf gedrückt → überschreibt den Effekt
                effectManualStrobe();
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
            delayTime = strobing ? 10 : audioFx ? 20 : (state.timerSpeed / 20);
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

    if (restartAt && (long)(millis() - restartAt) >= 0) {
        StateLock lock; // wartet, bis ein laufender Speichervorgang fertig ist
        Serial.println("Neustart …");
        delay(100);
        ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(10)); 
}
