#include <Arduino.h>
#include <SparkFunDMX.h>
#include "state.h"

#define DMX_TX_PIN 32
#define DMX_DIR_PIN 33

SparkFunDMX dmx;

extern int currentColorStep;
extern unsigned long dropStrobeUntil;

// ------------------------------------------------------------
// DMX Init
// ------------------------------------------------------------
void initDMX() {
    Serial2.begin(250000, SERIAL_8N2, -1, DMX_TX_PIN);
    dmx.begin(Serial2, DMX_DIR_PIN, 512);
    dmx.setComDir(DMX_WRITE_DIR);
}

// ------------------------------------------------------------
// Helper: Extract RGB from 0xRRGGBB
// ------------------------------------------------------------
void unpackColor(uint32_t col, uint8_t &r, uint8_t &g, uint8_t &b) {
    r = (col >> 16) & 0xFF;
    g = (col >> 8) & 0xFF;
    b = col & 0xFF;
}

// ------------------------------------------------------------
// DMX Task
// ------------------------------------------------------------
void taskDMX(void *pv) {
    initDMX();

    for (;;) {

        bool strobeActive = false;

        if (state.strobeMode)
            strobeActive = true;

        if (millis() < dropStrobeUntil)
            strobeActive = true;

        for (int lamp = 0; lamp < 4; lamp++) {

            int base = lamp * 7;

            dmx.writeByte(state.masterDimmer, base + 1);

            uint32_t col;

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

            dmx.writeByte(r, base + 2);
            dmx.writeByte(g, base + 3);
            dmx.writeByte(b, base + 4);

            if (strobeActive)
                dmx.writeByte(state.strobeLevel, base + 5);
            else
                dmx.writeByte(0, base + 5);
        }

        for (int i = 0; i < 32; i++)
            state.dmxPreview[i] = dmx.readByte(i + 1);

        dmx.update();
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}
