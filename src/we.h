#pragma once
#include <Arduino.h>

// ---------------------------------------------------------
// WEB INITIALIZATION + TASK
// ---------------------------------------------------------

// Startet den Webserver, richtet API‑Routen ein,
// lädt HTML/CSS/JS aus dem /data‑Ordner (LittleFS)
void web_init();

// Webserver‑Task (läuft auf eigenem Core)
void web_task(void *pvParameters);
