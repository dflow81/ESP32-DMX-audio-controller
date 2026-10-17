#pragma once

// Hotspot, falls kein WLAN eingerichtet oder erreichbar ist.
// Das Passwort muss mindestens 8 Zeichen haben – bitte ändern!
#define AP_SSID     "DMX-Controller"
#define AP_PASSWORD "12345678"

// Verbindet mit dem gespeicherten WLAN, sonst Hotspot
void setupWiFi();

// Hält die Verbindung aufrecht (in loop() aufrufen):
// versucht regelmäßig neu zu verbinden und schaltet den Hotspot bei Bedarf an/aus
void handleWiFi();
