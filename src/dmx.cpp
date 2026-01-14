#include "dmx.h"

#define DMX_TX_PIN 32
#define DMX_UNIVERSE_SIZE 128

// DMX Port
dmx_port_t dmxPort = DMX_NUM_1;

// DMX Datenpuffer
uint8_t dmxData[DMX_UNIVERSE_SIZE];

// 4 Lampen, jeweils 5 Kanäle
Lamp lamps[4] = {
    {1, 0, 0, 0, 0},
    {8, 0, 0, 0, 0},
    {15, 0, 0, 0, 0},
    {22, 0, 0, 0, 0}
};

float masterDimmer = 1.0f;

// ---------------------------------------------------------
// DMX INITIALISIERUNG
// ---------------------------------------------------------
void dmx_init() {
    dmx_config_t config = DMX_CONFIG_DEFAULT;
    dmx_param_config(dmxPort, &config);

    dmx_set_pin(dmxPort, DMX_TX_PIN, DMX_PIN_NO_CHANGE, DMX_PIN_NO_CHANGE);

    dmx_driver_install(dmxPort, DMX_INTR_FLAGS_DEFAULT);

    Serial.println("[DMX] Initialisiert");
}

// ---------------------------------------------------------
// DMX BUFFER UPDATE
// ---------------------------------------------------------
void dmx_update_buffer() {
    memset(dmxData, 0, DMX_UNIVERSE_SIZE);

    for (int i = 0; i < 4; i++) {
        uint16_t base = lamps[i].baseChannel;

        // Kanal 1: Dimmer
        dmxData[base - 1] = (uint8_t)(255 * masterDimmer);

        // Kanal 2–4: RGB
        dmxData[base + 1 - 1] = lamps[i].r;
        dmxData[base + 2 - 1] = lamps[i].g;
        dmxData[base + 3 - 1] = lamps[i].b;

        // Kanal 5: Strobe
        dmxData[base + 4 - 1] = lamps[i].strobe;
    }
}

// ---------------------------------------------------------
// OPTIONAL: DMX TASK (falls separat genutzt)
// ---------------------------------------------------------
void dmx_task(void *pvParameters) {
    for (;;) {
        dmx_update_buffer();
        dmx_write(dmxPort, dmxData, DMX_UNIVERSE_SIZE);
        dmx_send(dmxPort, DMX_UNIVERSE_SIZE);
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}
