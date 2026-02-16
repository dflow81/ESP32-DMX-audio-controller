#pragma once
#include <Arduino.h>

// Initialisiert die DMX‑Schnittstelle (UART2, Pins etc.)
void setupDMX();

// Berechnet die BPM-basierten Bewegungen für aktive Moving Heads
void updateMovements();

// Schreibt den aktuellen DMX‑Puffer auf die Leitung (inkl. Master-Berechnung)
void updateDMX();

// Wendet den Master‑Dimmer auf einen Wert an
uint8_t applyMaster(uint8_t v);

// Hilfsfunktion: Prüft ob ein Kanal dimmbar ist (Dimmer/RGBW) oder ein Steuerkanal (Pan/Tilt)
bool isDimmableChannel(int channelIndex);

// Setzt die Farbe für ein Fixture (berücksichtigt Offsets aus der Definition)
void setFixtureColor(int index, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

// NEU: Setzt Pan und Tilt für ein Fixture (0-255)
void setFixtureMovement(int index, uint8_t pan, uint8_t tilt);

// Konstanten für die DMX-Schnittstelle
#define DMX_BAUD 250000
#define DMX_FORMAT SERIAL_8N2
