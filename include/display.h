#pragma once

// 0,96" OLED 128x64 (SSD1306) per I2C
#define DISPLAY_SDA_PIN 22
#define DISPLAY_SCL_PIN 21
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64

// Initialisiert das Display (vor dem WLAN aufrufen, zeigt "startet …").
// Ohne angeschlossenes Display passiert einfach nichts.
void setupDisplay();

// Startet den Display-Task auf Core 0 (zeichnet ~10x pro Sekunde).
// Die I2C-Übertragung (~25 ms pro Bild) belastet so nicht Core 1 mit Effekten und Webserver.
void startDisplayTask();
