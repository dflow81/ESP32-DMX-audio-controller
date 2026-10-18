#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "display.h"
#include "state.h"
#include "netinput.h"

#define DISPLAY_REFRESH_MS 100   // ~10 frames per second
#define BEAT_FLASH_MS      120   // How long the beat dot stays lit

static Adafruit_SSD1306 oled(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
static bool displayOk = false;

// Checks whether an I2C device answers at the address
static bool i2cPresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void setupDisplay() {
    Wire.begin(DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
    Wire.setClock(400000);

    // Most modules use 0x3C, some 0x3D
    uint8_t addr = i2cPresent(0x3C) ? 0x3C : (i2cPresent(0x3D) ? 0x3D : 0);
    if (!addr || !oled.begin(SSD1306_SWITCHCAPVCC, addr)) {
        Serial.println("Display: nicht gefunden (läuft ohne weiter)");
        return;
    }
    displayOk = true;

    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(2);
    oled.setCursor(0, 8);
    oled.print("DMX");
    oled.setCursor(0, 28);
    oled.print("Controller");
    oled.setTextSize(1);
    oled.setCursor(0, 52);
    oled.print(state.langEn ? "starting ..." : "startet ...");
    oled.display();
    Serial.printf("Display: SSD1306 an 0x%02X\n", addr);
}

// Effect name as in the web UI: display names, custom chases, every word capitalized
static String displayEffectName(String e) {
    if (e == "lightorgan" || e == "retro") e = "light organ";
    else if (e == "snareflash") e = "snare flash";
    else if (e == "basswave") e = "bass wave";
    else if (e.startsWith("user")) {
        int slot = e.substring(4).toInt() - 1;
        StateLock lock;
        if (slot >= 0 && slot < USER_CHASES && state.userChases[slot].name.length()) e = state.userChases[slot].name;
    }
    for (int i = 0; i < (int)e.length(); i++) {
        if (i == 0 || e[i - 1] == ' ') e.setCharAt(i, toupper(e[i]));
    }
    return e;
}

static void drawDisplay() {
    unsigned long now = millis();

    // Copy the effect name briefly under lock (the string is changed by the web server)
    String effect;
    {
        StateLock lock;
        effect = state.currentEffect;
    }
    bool net = netInputActive();

    oled.clearDisplay();
    oled.setTextSize(1);

    // --- Line 1: network ---
    oled.setCursor(0, 0);
    if (WiFi.isConnected()) {
        oled.print(WiFi.localIP());
    } else if (WiFi.getMode() & WIFI_MODE_AP) {
        oled.print("AP ");
        oled.print(WiFi.softAPIP());
    } else {
        oled.print(state.langEn ? "no Wi-Fi" : "kein WLAN");
    }

    // --- Large BPM + beat dot ---
    int bpm = (isnan(state.bpm) || state.bpm < 10) ? 0 : (int)roundf(state.bpm);
    oled.setTextSize(3);
    oled.setCursor(0, 13);
    if (bpm > 0) oled.print(bpm);
    else oled.print("---");
    oled.setTextSize(1);
    oled.setCursor(62, 27);
    oled.print("BPM");

    bool beatOn = (now - state.lastBeatMs) < BEAT_FLASH_MS;
    if (beatOn) oled.fillCircle(115, 22, 8, SSD1306_WHITE);
    else        oled.drawCircle(115, 22, 8, SSD1306_WHITE);

    // --- Effect ---
    oled.setCursor(0, 39);
    if (net) {
        oled.print(state.langEn ? "PC control" : "PC-Steuerung");
    } else {
        effect = displayEffectName(effect);
        if (effect.length() > 21) effect = effect.substring(0, 21);
        oled.print(effect);
    }

    // --- Status line (inverted boxes for active states) ---
    struct Flag { const char *text; bool on; };
    Flag flags[] = {
        {state.langEn ? "LOCK" : "FEST", state.bpmLocked},
        {"DROP",  state.drop},
        {"PC",    net},
        {"BLACK", state.blackout},
        {"STBY",  state.standbyActive},
        {state.langEn ? "OFF" : "AUS", state.scheduleDark && !state.scheduleOverride},   // schedule
        {state.langEn ? "FOG" : "NEBEL", state.fogChannel > 0 && ((long)(state.fogUntil - now) > 0 || (long)(state.manualFogUntil - now) > 0)},
    };
    int x = 0;
    for (auto &f : flags) {
        if (!f.on) continue;
        int w = strlen(f.text) * 6 + 3;
        if (x + w > DISPLAY_WIDTH) break;   // doesn't fit in the line anymore
        oled.fillRect(x, 48, w, 9, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(x + 2, 49);
        oled.print(f.text);
        oled.setTextColor(SSD1306_WHITE);
        x += w + 3;
    }

    // --- Level bar ---
    float level = constrain(state.level / max(1.0, state.refLevel), 0.0, 1.0);
    oled.drawRect(0, 59, DISPLAY_WIDTH, 5, SSD1306_WHITE);
    oled.fillRect(0, 59, (int)(level * DISPLAY_WIDTH), 5, SSD1306_WHITE);

    oled.display();
}

// Burn-in protection: without music and interaction dim after half the time, then turn off.
// Returns false when the display is off (then don't draw).
static bool updateDisplayPower() {
    static int mode = 0;   // 0 = normal, 1 = dimmed, 2 = off
    unsigned long now = millis();
    unsigned long active = max((unsigned long)state.lastAudioKickMs, (unsigned long)state.lastUserMs);
    unsigned long idle = now - active;
    unsigned long offMs = (unsigned long)state.displayOffMin * 60000UL;

    int want = 0;
    if (offMs > 0 && idle >= offMs) want = 2;
    else if (offMs > 0 && idle >= offMs / 2) want = 1;
    if (want == mode) return mode != 2;

    if (want == 2) {
        oled.ssd1306_command(SSD1306_DISPLAYOFF);
    } else {
        if (mode == 2) oled.ssd1306_command(SSD1306_DISPLAYON);
        oled.dim(want == 1);
    }
    mode = want;
    return mode != 2;
}

static void displayTask(void *param) {
    for (;;) {
        uint32_t t0 = micros();
        if (updateDisplayPower()) drawDisplay();
        loadStats.displayUs = micros() - t0;
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
    }
}

void startDisplayTask() {
    if (!displayOk) return;   // no display → no task
    xTaskCreatePinnedToCore(displayTask, "displayTask", 4096, NULL, PRIO_DISPLAY,
                            &loadStats.displayTask, CORE_AUDIO);
}
