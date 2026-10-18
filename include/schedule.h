#pragma once
#include <Arduino.h>

// Schedule: lights only between state.scheduleStart and state.scheduleEnd.
// The time comes via NTP from the internet (German time incl. daylight saving).
// Without a valid time the lights stay on.
#define SCHEDULE_TZ  "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"

void setupSchedule();

// Call in loop() (checks once per second)
void handleSchedule();

// Current time as "HH:MM" (empty if no time yet)
String currentTimeString();
