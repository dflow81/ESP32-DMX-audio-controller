#pragma once

// Zugangsdaten für OTA-Updates (Web-Seite /update und PlatformIO-Upload per WLAN).
// Wenn du das Passwort änderst, auch "--auth=" in platformio.ini [env:esp32dev_ota] anpassen!
#define OTA_HOSTNAME "dmx-controller"   // erreichbar als http://dmx-controller.local
#define OTA_USER     "admin"
#define OTA_PASSWORD "dmxupdate"

// Startet ArduinoOTA und registriert die Update-Seite /update am Webserver
void setupOTA();

// Muss regelmäßig aufgerufen werden (in loop())
void handleOTA();
