#pragma once
#include <Arduino.h>

// Zeitplan: Licht nur zwischen state.scheduleStart und state.scheduleEnd.
// Die Uhrzeit kommt per NTP aus dem Internet (deutsche Zeit inkl. Sommerzeit).
// Ohne gültige Uhrzeit bleibt das Licht an.
#define SCHEDULE_TZ  "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"

void setupSchedule();

// In loop() aufrufen (prüft einmal pro Sekunde)
void handleSchedule();

// Aktuelle Uhrzeit als "HH:MM" (leer, wenn noch keine Zeit)
String currentTimeString();
