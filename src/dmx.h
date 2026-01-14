#pragma once
#include <Arduino.h>
#include <esp_dmx.h>

// ---------------------------------------------------------
// RGB STRUCT
// ---------------------------------------------------------
struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// ---------------------------------------------------------
// LAMP STRUCT
// ---------------------------------------------------------
struct Lamp {
    uint16_t baseChannel;   // Startkanal der Lampe
    uint8_t r, g, b;        // aktuelle RGB-Werte
    uint8_t strobe;         // Strobe-Wert
};

// ---------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------
extern Lamp lamps[4];
extern float masterDimmer;

extern dmx_port_t dmxPort;
extern uint8_t dmxData[];

// ---------------------------------------------------------
// FUNCTIONS
// ---------------------------------------------------------
void dmx_init();
void dmx_update_buffer();
void dmx_task(void *pvParameters);
