#pragma once

// Credentials for OTA updates (web page /update and PlatformIO upload via Wi-Fi).
// If you change the password, also adjust "--auth=" in platformio.ini [env:esp32dev_ota]!
#define OTA_HOSTNAME "dmx-controller"   // reachable as http://dmx-controller.local
#define OTA_USER     "admin"
#define OTA_PASSWORD "dmxupdate"

// Starts ArduinoOTA and registers the update page /update on the web server
void setupOTA();

// Must be called regularly (in loop())
void handleOTA();
