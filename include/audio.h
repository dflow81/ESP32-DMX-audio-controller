#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// FreeRTOS-Task für Mikrofon, FFT, Beat- und Drop-Erkennung (läuft auf Core 0)
void taskAudio(void *pv);

// --- Mikrofon einmessen ---
// Schritt 1: Stille messen (Rauschen), Schritt 2: laute Musik messen.
// Danach applyCalibration(): setzt Rauschschwellen und "laut"-Referenzen.
#define CAL_SILENCE 1
#define CAL_MUSIC   2
void startCalibration(int phase, unsigned long durationMs);
void cancelCalibration();
bool applyCalibration(String &message);   // Aufrufer hält den StateLock
void calibrationStatus(JsonObject o);
