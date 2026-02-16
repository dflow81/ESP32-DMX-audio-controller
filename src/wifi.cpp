
#include <WiFi.h>
#include "state.h"

extern ControllerState state;

void setupWiFi() {

    Serial.println("Starte WLAN...");

    // Wenn keine Daten gespeichert → AP starten
    if (state.wifi_ssid.length() == 0) {
        Serial.println("Keine WLAN-Daten gespeichert → Starte Access Point");

        WiFi.mode(WIFI_AP);
        WiFi.softAP("DMX-Controller", "12345678");

        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());
        return;
    }

    // Mit gespeichertem WLAN verbinden
    Serial.printf("Verbinde mit WLAN: %s\n", state.wifi_ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(state.wifi_ssid.c_str(), state.wifi_pass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
        delay(200);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWLAN verbunden!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWLAN fehlgeschlagen → Starte Access Point");

        WiFi.mode(WIFI_AP);
        WiFi.softAP("DMX-Controller", "12345678");

        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());
    }
}
