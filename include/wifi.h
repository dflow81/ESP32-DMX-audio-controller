#pragma once

// Hotspot if no Wi-Fi is configured or reachable.
// The password must have at least 8 characters – please change it!
#define AP_SSID     "DMX-Controller"
#define AP_PASSWORD "12345678"

// Connects to the saved Wi-Fi, otherwise hotspot
void setupWiFi();

// Keeps the connection alive (call in loop()):
// retries regularly and turns the hotspot on/off as needed
void handleWiFi();
