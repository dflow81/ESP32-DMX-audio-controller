#include <Arduino.h>
#include <LittleFS.h>
#include "state.h"
#include "dmx.h"
#include "audio.h"
#include "web.h"
#include "effects.h"
#include "wifi.h"

// 1. Extern bekannt machen
extern void taskAudio(void *pv); 
extern void handleAutoSave(); // Die Funktion aus der web.cpp bekannt machen

// 2. Effekt Task (Core 1)
void effectTask(void *param) {
    while (true) {
        // --- SPEICHER-CHECK ---
        // Prüft alle paar Millisekunden, ob seit 5 Sek. nichts mehr am Webinterface 
        // gedreht wurde und speichert dann einmalig den Flash-Speicher.
        handleAutoSave(); 

        // --- EFFEKT-LOGIK ---
        
        if (state.currentEffect == "disco") {
            effectDisco();
            updateDMX();
        } 
           
        else if (state.currentEffect == "disco2") {
             effectDisco2();
             updateDMX();
        }

        else if (state.currentEffect == "retro") {
            effectRetro();
            updateDMX();
        }
        else if (state.currentEffect == "static") {
            effectStaticColor();
            updateDMX();
        }
        else if (state.currentEffect != "none" && state.currentEffect != "") {
            runEffect(state.currentEffect);
            updateDMX();
        }

        // Dynamisches Delay
        int delayTime = (state.currentEffect == "disco" || state.currentEffect == "disco2" || state.currentEffect == "retro") ? 20 : (state.timerSpeed / 20);
        vTaskDelay(pdMS_TO_TICKS(max(10, delayTime)));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    
    if (!LittleFS.begin(true)) Serial.println("LittleFS Error!");

    initState();
    loadConfig(); // Lädt beim Start alle gespeicherten Werte
    updateDMXChannelCount();
    setupDMX();
    setupWiFi(); 
    setupWeb(); 

    // Audio Task auf CORE 0
    xTaskCreatePinnedToCore(taskAudio, "AudioTask", 10240, NULL, 2, NULL, 0);

    // Effekt Task auf CORE 1 (beinhaltet nun auch AutoSave)
    xTaskCreatePinnedToCore(effectTask, "effectTask", 4096, NULL, 1, NULL, 1);

    Serial.println("System ready.");
}

void loop() {
    // Da wir FreeRTOS Tasks nutzen, kann die Loop leer bleiben oder für 
    // sehr langsame Hintergrundprozesse genutzt werden.
    vTaskDelay(pdMS_TO_TICKS(1000)); 
}
