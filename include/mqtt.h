#pragma once

// MQTT / Home Assistant: Der Controller meldet sich per MQTT-Discovery automatisch
// in Home Assistant an – als Licht (an/aus = Blackout, Helligkeit = Master, Farbe,
// Effekte), als Auswahl für die Presets und als BPM-Sensor.
// Läuft in einem eigenen Task, damit ein nicht erreichbarer Broker nichts blockiert.

void setupMqtt();

// Nach Änderung der MQTT-Einstellungen aufrufen (verbindet neu)
void mqttReconfigure();
