#include "web.h"
#include "web_util.h"
#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "ota.h"
#include "dmx.h"
#include "netinput.h"
#include "audio.h"
#include "schedule.h"
#include "mqtt.h"

// Definition des Webservers
AsyncWebServer server(80);  
AsyncWebSocket ws("/ws");   // Live-Status (Pegel, Beat, Drop, BPM) an die Browser

volatile unsigned long lastChangeTime = 0;
volatile bool configDirty = false;
volatile bool configSaveNow = false;


void markConfigDirty() {
    lastChangeTime = millis();
    configDirty = true;
    state.userTouched = true; // Bedienung → Standby verlassen
    state.lastUserMs = millis();
}

// Wird vom Effekt-Task aufgerufen (hält dabei den StateLock)
void handleAutoSave() {
    if (configSaveNow || (configDirty && (millis() - lastChangeTime > 5000))) {
        configSaveNow = false;
        configDirty = false;
        saveConfig();
        Serial.println(">>> Flash: Auto-Save (Farbe/Master/MH-Bounds) erledigt.");
    }
}

// Sammelt einen Request-Body, der in mehreren Stücken ankommen kann.
// Gibt den kompletten Body zurück, sobald das letzte Stück da ist, sonst nullptr.
// Bei Fehlern wird direkt geantwortet und ebenfalls nullptr zurückgegeben.
const char* collectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len,
                               size_t index, size_t total) {
    if (total == 0 || total > MAX_CONFIG_BODY) {
        if (index == 0) req->send(413, "text/plain", "Body zu groß");
        return nullptr;
    }
    if (index == 0) req->_tempObject = malloc(total);
    if (!req->_tempObject) {
        if (index == 0) req->send(500, "text/plain", "Kein Speicher");
        return nullptr;
    }
    memcpy((uint8_t*)req->_tempObject + index, data, len);
    if (index + len < total) return nullptr; // Warten auf weitere Stücke
    return (const char*)req->_tempObject;
}

void hexToRGB(String hex, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (hex.startsWith("#")) hex = hex.substring(1);
    long number = strtol(hex.c_str(), NULL, 16);
    r = (number >> 16) & 0xFF;
    g = (number >> 8) & 0xFF;
    b = number & 0xFF;
}

String buildStatusJson(bool beat) {
    JsonDocument doc;
    {
        // Aktueller Effekt + Preset, damit alle Browser die Auswahl richtig anzeigen –
        // auch wenn Auto-Show, Cue-Liste, Taster o. Ä. gewechselt haben
        StateLock lock;
        doc["effect"] = state.currentEffect;
        doc["preset"] = state.activePreset;
    }
    doc["level"]  = constrain(state.level / max(1.0, state.refLevel), 0.0, 1.0);
    doc["beat"]   = beat;
    doc["drop"]   = state.drop;
    doc["bpm"]    = (isnan(state.bpm) || state.bpm < 10) ? 0 : (int)state.bpm;
    doc["master"] = state.master;
    doc["blackout"] = state.blackout;
    doc["bpmLocked"] = state.bpmLocked;
    doc["netActive"] = netInputActive();
    doc["standby"]   = state.standbyActive;
    doc["scheduleDark"] = state.scheduleDark && !state.scheduleOverride;
    doc["energy"]    = state.musicEnergy;
    doc["sync"]      = state.syncRole;
    doc["syncRx"]    = state.syncReceiving;
    doc["mqtt"]      = state.mqttConnected;
    doc["cueRunning"] = state.cueRunning;
    doc["cueIndex"]  = state.cueIndex;
    unsigned long now = millis();
    doc["fog"] = state.fogChannel > 0 &&
                 ((long)(state.fogUntil - now) > 0 || (long)(state.manualFogUntil - now) > 0);
    JsonArray spec = doc["spec"].to<JsonArray>();
    for (int i = 0; i < 16; i++) spec.add(state.spectrum[i]);
    doc["audioThreshold"] = state.audioThreshold; // Für den Slider-Sync

    String json;
    serializeJson(doc, json);
    return json;
}

void handleLiveStatus() {
    static unsigned long lastPush = 0, lastCleanup = 0;
    static uint32_t lastBeatCount = 0;
    static bool lastDrop = false, lastBlackout = false;

    unsigned long now = millis();
    if (now - lastCleanup > 1000) {
        ws.cleanupClients(); // getrennte Browser aufräumen
        lastCleanup = now;
    }
    if (ws.count() == 0) return;

    uint32_t beats = state.beatCount;
    bool beat = beats != lastBeatCount;
    bool changed = beat || state.drop != lastDrop || state.blackout != lastBlackout;
    if (!changed && now - lastPush < 100) return;
    if (!ws.availableForWriteAll()) return; // ein Browser hängt → diesen Durchlauf auslassen

    ws.textAll(buildStatusJson(beat));
    lastPush = now;
    lastBeatCount = beats;
    lastDrop = state.drop;
    lastBlackout = state.blackout;
}

// Fügt ein Preset als JSON-Objekt an (für /api/presets)
void presetToJson(JsonArray arr, int id, const Preset &p) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]     = id;
    o["name"]   = p.name;
    o["effect"] = p.effect;
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", p.r, p.g, p.b);
    o["color"]  = hex;
    o["speed"]  = p.timerSpeed;
    o["master"] = p.master;
    o["energy"] = p.energy;
}

bool adminAuthorized(AsyncWebServerRequest *req) {
    if (authActive()) return true;   // hat die Middleware schon geprüft
    if (req->authenticate(OTA_USER, OTA_PASSWORD)) return true;
    req->requestAuthentication();
    return false;
}

void setupWeb() {
    // Passwortschutz für alles (Seiten, APIs, WebSocket) – nur wenn eingeschaltet.
    // Basic-Auth, weil Browser sie zuverlässig auch beim WebSocket mitschicken.
    server.addMiddleware([](AsyncWebServerRequest *req, ArMiddlewareNext next) {
        if (authActive() && !req->authenticate(state.authUser.c_str(), state.authPass.c_str())) {
            req->requestAuthentication(AsyncAuthType::AUTH_BASIC, "DMX-Controller");
            return;
        }
        next();
    });

    // --- Statische Dateien (Lokal vom ESP32) ---
    server.serveStatic("/moving_head.html", LittleFS, "/moving_head.html");
    server.serveStatic("/nipplejs.min.js", LittleFS, "/nipplejs.min.js");
    server.serveStatic("/style.css", LittleFS, "/style.css");
    server.serveStatic("/index.js", LittleFS, "/index.js");
    server.serveStatic("/logo.svg", LittleFS, "/logo.svg");

    // API: Live Status (inkl. Audio-Daten für UI)
    // (Fallback, falls der WebSocket nicht verbunden ist)
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req){
        bool beat = (millis() - state.lastBeatMs) < 150; // kurz "halten", damit die UI ihn sieht
        req->send(200, "application/json", buildStatusJson(beat));
    });

    // Routen nach Themen (siehe web_show.cpp, web_fixtures.cpp, web_system.cpp)
    registerShowRoutes();
    registerFixtureRoutes();
    registerSystemRoutes();

    // WebSocket für den Live-Status
    server.addHandler(&ws);

    // Navigation & Default Pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/index.html", "text/html"); });
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/wifi.html", "text/html"); });
    server.on("/dmxconfig", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/dmxconfig.html", "text/html"); });
    server.on("/mh", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/moving_head.html", "text/html"); });
    server.on("/monitor", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/monitor.html", "text/html"); });
    server.on("/lauflicht", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/lauflicht.html", "text/html"); });

    setupOTA(); // Update-Seite /update + ArduinoOTA

    server.begin();
    Serial.println("Webserver mit allen MH & Audio Erweiterungen gestartet.");
}
