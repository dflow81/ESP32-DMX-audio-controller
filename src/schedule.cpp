#include <Arduino.h>
#include <time.h>
#include "schedule.h"
#include "state.h"

void setupSchedule() {
    // SNTP läuft im Hintergrund und versucht es weiter, bis das WLAN verbunden ist
    configTzTime(SCHEDULE_TZ, NTP_SERVER_1, NTP_SERVER_2);
}

// "HH:MM" → Minuten seit Mitternacht (-1 bei ungültiger Angabe)
static int parseTime(const String &s) {
    int colon = s.indexOf(':');
    if (colon < 1) return -1;
    int h = s.substring(0, colon).toInt();
    int m = s.substring(colon + 1).toInt();
    if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

String currentTimeString() {
    struct tm t;
    if (!getLocalTime(&t, 0)) return "";
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return String(buf);
}

void handleSchedule() {
    static unsigned long lastCheck = 0;
    static int lastInside = -1;     // -1 = noch nicht ausgewertet
    unsigned long nowMs = millis();
    if (nowMs - lastCheck < 1000) return;
    lastCheck = nowMs;

    struct tm t;
    state.timeValid = getLocalTime(&t, 0);
    if (!state.timeValid || !state.scheduleOn) {
        state.scheduleDark = false;   // ohne Uhrzeit oder Zeitplan: Licht an
        state.scheduleOverride = false;
        lastInside = -1;
        return;
    }

    int start = parseTime(state.scheduleStart);
    int end   = parseTime(state.scheduleEnd);
    if (start < 0 || end < 0 || start == end) {
        state.scheduleDark = false;
        return;
    }
    int now = t.tm_hour * 60 + t.tm_min;
    // Zeitfenster darf über Mitternacht gehen (z. B. 18:00 – 02:00)
    bool inside = (start < end) ? (now >= start && now < end) : (now >= start || now < end);

    if (lastInside == -1) {
        // Erste Auswertung (Start/Einschalten): nur Zustand übernehmen, kein Preset
        state.scheduleDark = !inside;
    } else if ((int)inside != lastInside) {
        // Wechsel: manuelles Einschalten gilt nur bis hierhin
        state.scheduleOverride = false;
        state.scheduleDark = !inside;
        if (inside && state.schedulePreset.length()) {
            StateLock lock;
            applyPreset(findPreset(state.schedulePreset), false);
        }
        Serial.printf("Zeitplan: Licht %s\n", inside ? "an" : "aus");
    }
    lastInside = inside;
}
