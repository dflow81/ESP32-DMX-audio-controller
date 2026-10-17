#pragma once
#include <Arduino.h>

// Steuerung durch PC-Lichtsoftware (QLC+, Freestyler, …) per Art-Net oder sACN (E1.31).
// Solange Daten für das eingestellte Universum ankommen, gibt der ESP diese 1:1 aus;
// bleiben sie NET_TIMEOUT_MS aus, läuft wieder die normale Effekt-Steuerung.
#define NET_TIMEOUT_MS 3000

// Startet/stoppt die UDP-Empfänger passend zu state.netInput / state.netUniverse.
// Nach jeder Änderung der Einstellungen erneut aufrufen.
void setupNetInput();

// true, wenn gerade Netzwerk-Daten ankommen
bool netInputActive();

// Kopiert das zuletzt empfangene DMX-Bild, gibt die Kanalanzahl zurück
int copyNetDmx(uint8_t *dst);
