#pragma once

// 0.96" OLED 128x64 (SSD1306) via I2C
#define DISPLAY_SDA_PIN 22
#define DISPLAY_SCL_PIN 21
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64

// Initializes the display (call before Wi-Fi, shows "starting …").
// Without a connected display nothing happens.
void setupDisplay();

// Starts the display task on core 0 (draws ~10x per second).
// That way the I2C transfer (~25 ms per frame) doesn't burden core 1 with effects and web server.
void startDisplayTask();
