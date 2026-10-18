#include <Arduino.h>
#include <time.h>
#include "schedule.h"
#include "state.h"

void setupSchedule() {
    // SNTP runs in the background and keeps trying until Wi-Fi is connected
    configTzTime(SCHEDULE_TZ, NTP_SERVER_1, NTP_SERVER_2);
}

// "HH:MM" → minutes since midnight (-1 if invalid)
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
    static int lastInside = -1;     // -1 = not evaluated yet
    unsigned long nowMs = millis();
    if (nowMs - lastCheck < 1000) return;
    lastCheck = nowMs;

    struct tm t;
    state.timeValid = getLocalTime(&t, 0);
    if (!state.timeValid || !state.scheduleOn) {
        state.scheduleDark = false;   // without time or schedule: lights on
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
    // The time window may cross midnight (e.g. 18:00 – 02:00)
    bool inside = (start < end) ? (now >= start && now < end) : (now >= start || now < end);

    if (lastInside == -1) {
        // First evaluation (start/enable): only take over the state, no preset
        state.scheduleDark = !inside;
    } else if ((int)inside != lastInside) {
        // Change: a manual override only lasts until here
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
