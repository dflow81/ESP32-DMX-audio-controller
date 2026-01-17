#include <Arduino.h>
#include <SparkFunDMX.h>
#include "state.h"

// ------------------------------------------------------------
// DMX Setup
// ------------------------------------------------------------
#define DMX_TX_PIN 32
#define DMX_DIR_PIN 33

SparkFunDMX dmx;

// ------------------------------------------------------------
// Hilfsfunktion: 24‑Bit Farbe → RGB
// ------------------------------------------------------------
static void unpackColor(uint32_t col, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = (col >> 16) & 0xFF;
    g = (col >> 8) & 0xFF;
    b = col & 0xFF;
}

// ------------------------------------------------------------
// DMX Initialisierung
// ------------------------------------------------------------
void initDMX() {
    Serial2.begin(250000, SERIAL_8N2, -1, DMX_TX_PIN);
    dmx.begin(Serial2, DMX_DIR_PIN, 512);
    dmx.setComDir(DMX_WRITE_DIR);
}

// ------------------------------------------------------------
// DMX Task
// ------------------------------------------------------------
void taskDMX(void *pv) {
    initDMX();

    for (;;) {

        bool strobeActive = false;

        // Strobe aktiv?
        if (state.strobeMode)
            strobeActive = true;

        // Drop‑Strobe (kurzer Flash)
        if (millis() < dropStrobeUntil)
            strobeActive = true;

        // 4 Lampen → je 7 Kanäle
        for (int lamp = 0; lamp < 4; lamp++) {

            int base = lamp * 7;

            // Kanal 1: Master Dimmer
            dmx.writeByte(state.masterDimmer, base + 1);

            // Farbe auswählen
            uint32_t col = 0;

            switch (state.mode) {

                case ControllerState::MODE_SOLID:
                    col = state.solidColors[lamp];
                    break;

                case ControllerState::MODE_BEAT_CHASE:
                    col = state.palette[(currentColorStep + lamp) % 8];
                    break;

                case ControllerState::MODE_BEAT_PULSE:
                    col = state.palette[(currentColorStep + lamp) % 8];
                    break;

                case ControllerState::MODE_STROBE:
                    col = 0xFFFFFF;
                    break;
            }

            uint8_t r, g, b;
            unpackColor(col, r, g, b);

            // RGB Kanäle
            dmx.writeByte(r, base + 2);
            dmx.writeByte(g, base + 3);
            dmx.writeByte(b, base + 4);

            // Strobe Kanal
            if (strobeActive)
                dmx.writeByte(state.strobeLevel, base + 5);
            else
                dmx.writeByte(0, base + 5);

            // Kanäle 6 & 7 ungenutzt → 0
            dmx.writeByte(0, base + 6);
            dmx.writeByte(0, base + 7);
        }

        // DMX Preview für WebUI
        for (int i = 0; i < 32; i++)
            state.dmxPreview[i] = dmx.readByte(i + 1);

        dmx.update();
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}
