#pragma once
#include <Arduino.h>

// Control by PC lighting software (QLC+, Freestyler, …) via Art-Net or sACN (E1.31).
// While data for the configured universe arrives, the ESP outputs it 1:1;
// if it stays away for NET_TIMEOUT_MS, the normal effect control runs again.
#define NET_TIMEOUT_MS 3000

// Starts/stops the UDP receivers according to state.netInput / state.netUniverse.
// Call again after every settings change.
void setupNetInput();

// true while network data is arriving
bool netInputActive();

// Copies the last received DMX frame, returns the channel count
int copyNetDmx(uint8_t *dst);
