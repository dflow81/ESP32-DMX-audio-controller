#pragma once

// Mehrere Controller synchron per ESP-NOW (direkt von ESP zu ESP, kein Router nötig).
// Der Master sendet Beat, Tempo, Effekt, Farben, Palette, Blackout, Strobe und Drops;
// Slaves übernehmen das für ihre eigenen Lampen. Alle Controller müssen im selben
// WLAN sein (ESP-NOW nutzt den Funkkanal des Routers) und dieselbe Sync-Gruppe haben.
#define SYNC_TIMEOUT_MS 1500   // so lange ohne Daten → Slave arbeitet wieder selbstständig

void setupSync();

// In loop() aufrufen: Master sendet (sofort bei Beat/Änderung, sonst alle 50 ms)
void handleSync();

// Im Effekt-Task aufrufen (mit StateLock): Slave übernimmt die empfangenen Daten
void applySyncFromMaster();
