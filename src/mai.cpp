#include <Arduino.h>
#include "state.h"
#include "web.h"

// Task Handles
TaskHandle_t taskAudioHandle = nullptr;
TaskHandle_t taskDMXHandle   = nullptr;

// Task-Funktionen aus anderen Dateien
void taskAudio(void *pv);
void taskDMX(void *pv);
void setupWiFi();
void setupWeb();

void setup() {
    Serial.begin(115200);
    delay(200);

    // Globalen Zustand initialisieren
    initState();

    // Netzwerk & Webserver starten
    setupWiFi();
    setupWeb();

    // Audio-Task auf Core 0
    xTaskCreatePinnedToCore(
        taskAudio,
        "Audio",
        8192,
        NULL,
        3,
        &taskAudioHandle,
        0
    );

    // DMX-Task auf Core 1
    xTaskCreatePinnedToCore(
        taskDMX,
        "DMX",
        4096,
        NULL,
        2,
        &taskDMXHandle,
        1
    );

    Serial.println("System gestartet: Audio + DMX + WebUI aktiv");
}

void loop() {
    // WebSocket: Live-State an alle Clients senden
    ws.textAll(buildStateJson());

    // UI-Update-Rate ~10 Hz
    delay(100);
}
