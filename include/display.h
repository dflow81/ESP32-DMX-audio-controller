#pragma once

// 0,96" OLED 128x64 (SSD1306) per I2C
#define DISPLAY_SDA_PIN 22
#define DISPLAY_SCL_PIN 21
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64

// Initialisiert das Display (vor dem WLAN aufrufen, zeigt "startet …").
// Ohne angeschlossenes Display passiert einfach nichts.
void setupDisplay();

// Zeichnet den aktuellen Status (in loop() aufrufen, aktualisiert ~10x pro Sekunde)
void handleDisplay();
