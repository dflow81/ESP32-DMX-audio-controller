#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "display.h"
#include "state.h"
#include "netinput.h"

#define DISPLAY_REFRESH_MS 100   // ~10 Bilder pro Sekunde
#define BEAT_FLASH_MS      120   // So lange leuchtet der Beat-Punkt

static Adafruit_SSD1306 oled(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
static bool displayOk = false;

// Prüft, ob unter der Adresse ein I2C-Gerät antwortet
static bool i2cPresent(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void setupDisplay() {
    Wire.begin(DISPLAY_SDA_PIN, DISPLAY_SCL_PIN);
    Wire.setClock(400000);

    // Die meisten Module nutzen 0x3C, manche 0x3D
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
    oled.print("startet ...");
    oled.display();
    Serial.printf("Display: SSD1306 an 0x%02X\n", addr);
}

void handleDisplay() {
    if (!displayOk) return;

    static unsigned long lastDraw = 0;
    unsigned long now = millis();
    if (now - lastDraw < DISPLAY_REFRESH_MS) return;
    lastDraw = now;

    // Effekt-Name kurz unter Lock kopieren (String wird vom Webserver geändert)
    String effect;
    {
        StateLock lock;
        effect = state.currentEffect;
    }
    bool net = netInputActive();

    oled.clearDisplay();
    oled.setTextSize(1);

    // --- Zeile 1: Netzwerk ---
    oled.setCursor(0, 0);
    if (WiFi.isConnected()) {
        oled.print(WiFi.localIP());
    } else if (WiFi.getMode() & WIFI_MODE_AP) {
        oled.print("AP ");
        oled.print(WiFi.softAPIP());
    } else {
        oled.print("kein WLAN");
    }

    // --- BPM groß + Beat-Punkt ---
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

    // --- Effekt ---
    oled.setCursor(0, 39);
    if (net) {
        oled.print("PC-Steuerung");
    } else {
        if (effect.length() > 21) effect = effect.substring(0, 21);
        oled.print(effect);
    }

    // --- Statuszeile (invertierte Kästchen für aktive Zustände) ---
    struct Flag { const char *text; bool on; };
    Flag flags[] = {
        {"FEST",  state.bpmLocked},
        {"DROP",  state.drop},
        {"PC",    net},
        {"BLACK", state.blackout},
    };
    int x = 0;
    for (auto &f : flags) {
        if (!f.on) continue;
        int w = strlen(f.text) * 6 + 3;
        oled.fillRect(x, 48, w, 9, SSD1306_WHITE);
        oled.setTextColor(SSD1306_BLACK);
        oled.setCursor(x + 2, 49);
        oled.print(f.text);
        oled.setTextColor(SSD1306_WHITE);
        x += w + 3;
    }

    // --- Pegelbalken ---
    float level = constrain(state.level / 5000.0f, 0.0f, 1.0f);
    oled.drawRect(0, 59, DISPLAY_WIDTH, 5, SSD1306_WHITE);
    oled.fillRect(0, 59, (int)(level * DISPLAY_WIDTH), 5, SSD1306_WHITE);

    oled.display();
}
