
#include <WiFi.h>
#include "state.h"
#include "wifi.h"

extern ControllerState state;

#define WIFI_CONNECT_TIMEOUT_MS  8000   // Wait this long for Wi-Fi at startup
#define WIFI_RETRY_MS           30000   // Without connection retry every 30 s
#define WIFI_AP_FALLBACK_MS     60000   // This long without Wi-Fi before the hotspot turns on

static bool apActive = false;
static unsigned long lastAttempt = 0;
static unsigned long lostSince = 0;

static void startAP() {
    // With saved Wi-Fi: AP+STA so it can keep connecting in the background
    WiFi.mode(state.wifi_ssid.length() > 0 ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    apActive = true;

    Serial.print("Hotspot aktiv: ");
    Serial.print(AP_SSID);
    Serial.print("  IP: ");
    Serial.println(WiFi.softAPIP());
}

static void stopAP() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActive = false;
    Serial.println("Hotspot aus (WLAN verbunden)");
}

static void connectSTA() {
    lastAttempt = millis();
    WiFi.begin(state.wifi_ssid.c_str(), state.wifi_pass.c_str());
}

void setupWiFi() {

    Serial.println("Starte WLAN...");
    WiFi.persistent(false);       // Don't additionally store the credentials in the ESP's internal flash
    WiFi.setAutoReconnect(true);  // the ESP handles short dropouts itself

    // No credentials saved → start AP
    if (state.wifi_ssid.length() == 0) {
        Serial.println("Keine WLAN-Daten gespeichert → Starte Access Point");
        startAP();
        return;
    }

    // Connect to the saved Wi-Fi
    Serial.printf("Verbinde mit WLAN: %s\n", state.wifi_ssid.c_str());

    WiFi.mode(WIFI_STA);
    connectSTA();

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
        delay(200);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWLAN verbunden!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWLAN fehlgeschlagen → Starte Access Point, versuche es im Hintergrund weiter");
        lostSince = millis();
        startAP();
    }
}

void handleWiFi() {
    if (state.wifi_ssid.length() == 0) return; // Hotspot-only operation, nothing to do

    unsigned long now = millis();

    if (WiFi.status() == WL_CONNECTED) {
        if (lostSince) {
            Serial.print("WLAN (wieder) verbunden, IP: ");
            Serial.println(WiFi.localIP());
            lostSince = 0;
        }
        // Only turn off the hotspot once nobody is connected through it
        if (apActive && WiFi.softAPgetStationNum() == 0) stopAP();
        return;
    }

    // No connection
    if (!lostSince) {
        lostSince = now;
        Serial.println("WLAN-Verbindung verloren");
    }
    if (!apActive && now - lostSince > WIFI_AP_FALLBACK_MS) startAP();
    if (now - lastAttempt > WIFI_RETRY_MS) {
        Serial.println("Versuche WLAN neu zu verbinden …");
        WiFi.disconnect();
        connectSTA();
    }
}
