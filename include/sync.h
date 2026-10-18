#pragma once

// Several controllers in sync via ESP-NOW (directly from ESP to ESP, no router needed).
// The master sends beat, tempo, effect, colors, palette, blackout, strobe and drops;
// slaves apply them to their own lamps. All controllers must be on the same
// Wi-Fi (ESP-NOW uses the router's radio channel) and in the same sync group.
#define SYNC_TIMEOUT_MS 1500   // this long without data → the slave works on its own again

void setupSync();

// Call in loop(): the master sends (immediately on beat/change, otherwise every 50 ms)
void handleSync();

// Call in the effect task (with StateLock): the slave applies the received data
void applySyncFromMaster();
