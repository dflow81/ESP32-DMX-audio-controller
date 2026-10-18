#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// FreeRTOS task for microphone, FFT, beat and drop detection (runs on core 0)
void taskAudio(void *pv);

// --- Microphone calibration ---
// Step 1: measure silence (noise), step 2: measure loud music.
// Then applyCalibration(): sets noise thresholds and "loud" references.
#define CAL_SILENCE 1
#define CAL_MUSIC   2
void startCalibration(int phase, unsigned long durationMs);
void cancelCalibration();
bool applyCalibration(String &message);   // caller holds the StateLock
void calibrationStatus(JsonObject o);
