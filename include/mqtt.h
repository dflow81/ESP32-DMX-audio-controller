#pragma once

// MQTT / Home Assistant: the controller registers itself automatically via MQTT discovery
// in Home Assistant – as a light (on/off = blackout, brightness = master, color,
// effects), as a preset selector and as a BPM sensor.
// Runs in its own task so an unreachable broker blocks nothing.

void setupMqtt();

// Call after changing the MQTT settings (reconnects)
void mqttReconfigure();
