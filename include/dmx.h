#pragma once
#include <Arduino.h>

// Initializes the DMX interface (UART2, pins etc.)
void setupDMX();

// Computes the BPM-based movements for active moving heads
void updateMovements();

// Writes the current DMX buffer to the line (incl. master calculation)
void updateDMX();

// Applies the master dimmer to a value
uint8_t applyMaster(uint8_t v);

// Helper: checks whether a channel is dimmable (dimmer/RGBW) or a control channel (pan/tilt)
bool isDimmableChannel(int channelIndex);

// Sets the color of a fixture (respects the offsets from the definition)
void setFixtureColor(int index, uint8_t r, uint8_t g, uint8_t b, uint8_t w);

// Sets pan and tilt of a fixture (0-255)
void setFixtureMovement(int index, uint8_t pan, uint8_t tilt);

// Smooth transitions: a change (effect, color, preset) is faded in over state.fadeMs
// (or once over state.nextFadeMs).
// Trigger via state.fadeRequest = true (also from other tasks).
bool isFading();
void cancelFade();

// Outputs a complete DMX frame from the network (Art-Net/sACN) unchanged.
// Only blackout still applies (safety); master and effects don't.
void updateDMXFromNetwork(const uint8_t *data, int len);

// Copies the last sent channel values (for the DMX monitor), returns the count
int getDMXOutput(uint8_t *buf, int maxLen);

// Constants for the DMX interface
#define DMX_BAUD 250000
#define DMX_FORMAT SERIAL_8N2
