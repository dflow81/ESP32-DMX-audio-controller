#pragma once
#include <Arduino.h>

// ---------------------------------------------------------
// GLOBAL AUDIO / BEAT VARIABLES
// ---------------------------------------------------------

// Audio level (RMS / average amplitude)
extern float level;

// Global beat flag (true = beat detected)
extern bool beatDetected;

// BPM estimate (updated continuously)
extern float bpmEstimate;

// Individual beat types
extern bool kick;
extern bool snare;
extern bool hihat;

// Spectral flux (used for beat detection)
extern float spectralFlux;

// ---------------------------------------------------------
// FUNCTIONS
// ---------------------------------------------------------

// Initialize I2S + FFT system
void audio_init();

// Audio processing task (runs on its own core)
void audio_task(void *pvParameters);
