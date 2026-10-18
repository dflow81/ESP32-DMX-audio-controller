#pragma once

// Buttons / footswitch on the ESP32: each between GPIO and GND (internal pull-ups).
// Unconnected pins do nothing. -1 = function without button.
#define BTN_STROBE_PIN    25   // hold  = strobe
#define BTN_BLACKOUT_PIN  26   // press = blackout on/off
#define BTN_FOG_PIN       27   // hold  = fog
#define BTN_TAP_PIN       14   // press = tap tempo
#define BTN_NEXT_PIN      13   // press = next preset

void setupButtons();

// Call in loop() (debounced, ~30 ms)
void handleButtons();
