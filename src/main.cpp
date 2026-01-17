#include <Arduino.h>
#include "state.h"
#include "web.h"

// Task Handles
TaskHandle_t taskAudioHandle = nullptr;
TaskHandle_t taskDMXHandle   = nullptr;

// Task-Funktionen aus audio.cpp und dmx.cpp
void taskAudio(void *pv);
void taskDMX(void *pv);

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("\n--- ESP32 DMX Audio Controller startet ---");

    // Globalen Zustand initialisieren
    initState();

    // Netzwerk + Webserver
    setupWiFi();
    setupWeb();

    // Audio-Task (Core 0)
    xTaskCreatePinnedToCore(
        taskAudio,
        "Audio",
        8192,
        NULL,
        3,
        &taskAudioHandle,
        0
    );

    // DMX-Task (Core 1)
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
    // WebSocket Broadcast alle 100ms
    ws.textAll(buildStateJson());
    delay(100);
}
