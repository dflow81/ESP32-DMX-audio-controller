
#include <WiFi.h>
#include "state.h"
#include "wifi.h"

extern ControllerState state;

#define WIFI_CONNECT_TIMEOUT_MS  8000   // Beim Start so lange auf das WLAN warten
#define WIFI_RETRY_MS           30000   // Ohne Verbindung alle 30 s neu versuchen
#define WIFI_AP_FALLBACK_MS     60000   // So lange ohne WLAN, bevor der Hotspot angeht

static bool apActive = false;
static unsigned long lastAttempt = 0;
static unsigned long lostSince = 0;

static void startAP() {
    // Mit gespeichertem WLAN: AP+STA, damit im Hintergrund weiter verbunden werden kann
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
    WiFi.persistent(false);       // Zugangsdaten nicht zusätzlich im ESP-internen Flash ablegen
    WiFi.setAutoReconnect(true);  // kurze Aussetzer fängt der ESP selbst ab

    // Wenn keine Daten gespeichert → AP starten
    if (state.wifi_ssid.length() == 0) {
        Serial.println("Keine WLAN-Daten gespeichert → Starte Access Point");
        startAP();
        return;
    }

    // Mit gespeichertem WLAN verbinden
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
    if (state.wifi_ssid.length() == 0) return; // Nur Hotspot-Betrieb, nichts zu tun

    unsigned long now = millis();

    if (WiFi.status() == WL_CONNECTED) {
        if (lostSince) {
            Serial.print("WLAN (wieder) verbunden, IP: ");
            Serial.println(WiFi.localIP());
            lostSince = 0;
        }
        // Hotspot erst abschalten, wenn niemand mehr darüber verbunden ist
        if (apActive && WiFi.softAPgetStationNum() == 0) stopAP();
        return;
    }

    // Keine Verbindung
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
