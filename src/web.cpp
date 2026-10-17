#include "web.h"
#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "ota.h"

// Definition des Webservers
AsyncWebServer server(80);  

volatile unsigned long lastChangeTime = 0;
volatile bool configDirty = false;
volatile bool configSaveNow = false;

// Max. Größe für /api/saveConfig (schützt vor zu großen Requests)
#define MAX_CONFIG_BODY 16384

void markConfigDirty() {
    lastChangeTime = millis();
    configDirty = true;
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
static const char* collectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len,
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

void setupWeb() {
    // --- Statische Dateien (Lokal vom ESP32) ---
    server.serveStatic("/moving_head.html", LittleFS, "/moving_head.html");
    server.serveStatic("/nipplejs.min.js", LittleFS, "/nipplejs.min.js");
    server.serveStatic("/style.css", LittleFS, "/style.css");
    server.serveStatic("/logo.svg", LittleFS, "/logo.svg");

    // API: Live Status (inkl. Audio-Daten für UI)
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req){
        JsonDocument doc;
        doc["level"]  = constrain(state.level / 5000.0f, 0.0f, 1.0f);
        doc["beat"]   = (millis() - state.lastBeatMs) < 150; // kurz "halten", damit die UI ihn sieht
        doc["drop"]   = state.drop;
        doc["bpm"]    = (isnan(state.bpm) || state.bpm < 10) ? 0 : (int)state.bpm;
        doc["master"] = state.master;
        doc["audioThreshold"] = state.audioThreshold; // Für den Slider-Sync

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // --- NEU: API für Audio Threshold (Noise Gate) ---
    server.on("/api/setThreshold", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("value")) {
            state.audioThreshold = req->getParam("value")->value().toInt();
            markConfigDirty();
            req->send(200, "text/plain", "OK");
        } else { req->send(400, "text/plain", "Error"); }
    });

    // API: Drop-Erkennung an/aus
    server.on("/api/setDrop", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("on")) {
            state.dropEnabled = req->getParam("on")->value().toInt() != 0;
            if (!state.dropEnabled) state.drop = false;
            markConfigDirty();
            req->send(200, "text/plain", "OK");
        } else { req->send(400, "text/plain", "Error"); }
    });

    // API: Strobe-Knopf. Die UI sendet on=1 wiederholt, solange der Knopf gedrückt ist;
    // bleibt das aus (Verbindung weg), endet der Strobe nach STROBE_KEEPALIVE_MS von selbst
    server.on("/api/strobe", HTTP_GET, [](AsyncWebServerRequest *req){
        const unsigned long STROBE_KEEPALIVE_MS = 600;
        if (req->hasParam("on")) {
            bool on = req->getParam("on")->value().toInt() != 0;
            state.manualStrobeUntil = on ? millis() + STROBE_KEEPALIVE_MS : 0;
            req->send(200, "text/plain", "OK");
        } else { req->send(400, "text/plain", "Error"); }
    });

    // --- NEU: API für BPM (für Motor-Test in der MH-UI) ---
    server.on("/api/setBPM", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("value")) {
            state.bpm = req->getParam("value")->value().toInt();
            req->send(200, "text/plain", "OK");
        } else { req->send(400, "text/plain", "Error"); }
    });

    // API: Moving Head Liste
    server.on("/api/fixtures", HTTP_GET, [](AsyncWebServerRequest *req){
        JsonDocument doc;
        JsonArray array = doc.to<JsonArray>();
        for (size_t i = 0; i < state.fixtures.size(); i++) {
            const FixtureDefinition* def = getFixtureDef(state.fixtures[i].type);
            if (def && def->offsetPan >= 0) {
                JsonObject obj = array.add<JsonObject>();
                obj["id"] = i;
                obj["name"] = state.fixtures[i].name;
                obj["start"] = state.fixtures[i].start;
                obj["isMH"] = true;
            }
        }
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: Joystick Movement
    server.on("/api/move", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("id") && req->hasParam("p") && req->hasParam("t")) {
            int id = req->getParam("id")->value().toInt();
            uint8_t p = req->getParam("p")->value().toInt();
            uint8_t t = req->getParam("t")->value().toInt();
            StateLock lock;
            if(id >= 0 && id < (int)state.fixtures.size()) {
                if (state.fixtures[id].isAutoMoving) {
                    state.fixtures[id].isAutoMoving = false; // Joystick übernimmt
                    markConfigDirty();
                }
                setFixtureMovement(id, p, t);
                req->send(200, "text/plain", "OK");
                return;
            }
        }
        req->send(400, "text/plain", "Error");
    });

    // API: Pan/Tilt Eckpunkte A/B speichern
    server.on("/api/save", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("id") && req->hasParam("pt")) {
            int id = req->getParam("id")->value().toInt();
            String pt = req->getParam("pt")->value();
            StateLock lock;
            if(id >= 0 && id < (int)state.fixtures.size()) {
                auto &fx = state.fixtures[id];
                const FixtureDefinition* def = getFixtureDef(fx.type);
                int base = fx.start - 1;
                if(def && def->offsetPan >= 0 && def->offsetTilt >= 0 &&
                   base + max(def->offsetPan, def->offsetTilt) < 512) {
                    if(pt == "A") { 
                        fx.panMin = state.dmx[base+def->offsetPan]; 
                        fx.tiltMin = state.dmx[base+def->offsetTilt]; 
                    } else { 
                        fx.panMax = state.dmx[base+def->offsetPan]; 
                        fx.tiltMax = state.dmx[base+def->offsetTilt]; 
                    }
                    markConfigDirty();
                    req->send(200, "text/plain", "OK");
                    return;
                }
            }
        }
        req->send(400, "text/plain", "Error");
    });

    // API: Moving Head ausgewählt → aktuellen Zustand zurückgeben
    server.on("/api/select", HTTP_GET, [](AsyncWebServerRequest *req){
        int id = req->hasParam("id") ? req->getParam("id")->value().toInt() : -1;
        StateLock lock;
        if (id < 0 || id >= (int)state.fixtures.size()) {
            req->send(400, "text/plain", "Error");
            return;
        }
        auto &fx = state.fixtures[id];
        JsonDocument doc;
        doc["id"]   = id;
        doc["name"] = fx.name;
        doc["auto"] = fx.isAutoMoving;
        doc["pMin"] = fx.panMin;  doc["pMax"] = fx.panMax;
        doc["tMin"] = fx.tiltMin; doc["tMax"] = fx.tiltMax;
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: Auto-Modus gezielt setzen (für den Motor-Test)
    server.on("/api/setAuto", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("id") && req->hasParam("on")) {
            int id = req->getParam("id")->value().toInt();
            StateLock lock;
            if (id >= 0 && id < (int)state.fixtures.size()) {
                state.fixtures[id].isAutoMoving = req->getParam("on")->value().toInt() != 0;
                markConfigDirty();
                req->send(200, "text/plain", state.fixtures[id].isAutoMoving ? "ON" : "OFF");
                return;
            }
        }
        req->send(400, "text/plain", "Error");
    });

    // API: Auto-Modus Toggle
    server.on("/api/toggleAuto", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("id")) {
            int id = req->getParam("id")->value().toInt();
            StateLock lock;
            if(id >= 0 && id < (int)state.fixtures.size()) {
                state.fixtures[id].isAutoMoving = !state.fixtures[id].isAutoMoving;
                markConfigDirty();
                req->send(200, "text/plain", state.fixtures[id].isAutoMoving ? "ON" : "OFF");
                return;
            }
        }
        req->send(400, "text/plain", "Error");
    });

    // API: Standard DMX Steuerung
    server.on("/api/setColor", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("hex")) {
            StateLock lock;
            hexToRGB(req->getParam("hex")->value(), state.colorR, state.colorG, state.colorB);
            state.currentEffect = "static"; 
            markConfigDirty();
        }
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/setMaster", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("value")) {
            state.master = constrain(req->getParam("value")->value().toInt(), 0, 255);
            markConfigDirty();
        }
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/setEffect", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("name")) {
            String name = req->getParam("name")->value();
            StateLock lock;
            state.currentEffect = name;
            markConfigDirty();
        }
        req->send(200, "text/plain", "OK");
    });
    // API: Effekt-Geschwindigkeit (Timer) setzen
    server.on("/api/setTimer", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("ms")) {
            state.timerSpeed = constrain(req->getParam("ms")->value().toInt(), TIMER_SPEED_MIN, TIMER_SPEED_MAX);
            // Damit der Wert nach Neustart bleibt:
            markConfigDirty(); 
            req->send(200, "text/plain", "OK");
        } else {
            req->send(400, "text/plain", "Parameter 'ms' fehlt");
        }
    });


    // API: Config abrufen
    server.on("/api/getConfig", HTTP_GET, [](AsyncWebServerRequest *req){
        StateLock lock;
        JsonDocument doc;
        JsonArray arr = doc["fixtures"].to<JsonArray>();
        for (auto &fx : state.fixtures) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = fx.name; o["start"] = fx.start; o["type"] = fx.type;
        }
        JsonObject types = doc["fixtureTypes"].to<JsonObject>();
        for (auto &kv : state.fixtureTypes) {
            JsonObject t = types[kv.first].to<JsonObject>();
            t["channels"] = kv.second.channels;
            t["offsetDimmer"] = kv.second.offsetDimmer;
            t["offsetR"] = kv.second.offsetR;
            t["offsetG"] = kv.second.offsetG;
            t["offsetB"] = kv.second.offsetB;
            t["offsetW"] = kv.second.offsetW;
            t["offsetPan"] = kv.second.offsetPan;
            t["offsetTilt"] = kv.second.offsetTilt;
        }
        doc["master"] = state.master;
        doc["effect"] = state.currentEffect;
        doc["audioThreshold"] = state.audioThreshold;
        doc["dropEnabled"] = state.dropEnabled;
        doc["timerSpeed"] = state.timerSpeed;
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02x%02x%02x", state.colorR, state.colorG, state.colorB);
        doc["color"] = hex;
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: Config speichern
    // Der Body kann in mehreren Stücken ankommen → erst sammeln, dann parsen
    server.on("/api/saveConfig", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
        const char *body = collectBody(req, data, len, index, total);
        if (!body) return;

        JsonDocument doc;
        if (deserializeJson(doc, body, total)) {
            req->send(400, "text/plain", "JSON Error");
            return;
        }

        StateLock lock;

        // Alte Fixtures merken, damit MH-Grenzen & Auto-Modus erhalten bleiben
        std::vector<Fixture> oldFixtures = state.fixtures;

        state.fixtures.clear();
        for (JsonObject o : doc["fixtures"].as<JsonArray>()) {
            Fixture fx;
            fx.name = o["name"].as<String>();
            fx.start = constrain(o["start"].as<int>(), 1, 512);
            fx.type = o["type"].as<String>();
            for (auto &old : oldFixtures) {
                if (old.name == fx.name) {
                    fx.panMin = old.panMin;   fx.panMax = old.panMax;
                    fx.tiltMin = old.tiltMin; fx.tiltMax = old.tiltMax;
                    fx.isAutoMoving = old.isAutoMoving;
                    break;
                }
            }
            state.fixtures.push_back(fx);
        }
        state.fixtureTypes.clear();
        for (JsonPair kv : doc["fixtureTypes"].as<JsonObject>()) {
            FixtureDefinition def;
            JsonObject t = kv.value().as<JsonObject>();
            def.channels = t["channels"] | 1;
            def.offsetDimmer = t["offsetDimmer"] | -1;
            def.offsetR = t["offsetR"] | -1;
            def.offsetG = t["offsetG"] | -1;
            def.offsetB = t["offsetB"] | -1;
            def.offsetW = t["offsetW"] | -1;
            def.offsetPan = t["offsetPan"] | -1;
            def.offsetTilt = t["offsetTilt"] | -1;
            state.fixtureTypes[kv.key().c_str()] = def;
        }
        updateDMXChannelCount();
        memset(state.dmx, 0, sizeof(state.dmx)); // Alte Werte auf verschobenen Adressen löschen

        // Flash-Schreiben und DMX-Neustart übernimmt der Effekt-Task
        state.dmxReinit = true;
        configSaveNow = true;
        req->send(200, "text/plain", "OK");
    });

    // API: WLAN-Netzwerke scannen
    // Der Scan läuft im Hintergrund: 202 = läuft noch (UI fragt erneut), 200 = Liste
    server.on("/api/scanWifi", HTTP_GET, [](AsyncWebServerRequest *req){
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED) {
            // Im reinen AP-Modus kann nicht gescannt werden → AP+STA
            if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
            WiFi.scanNetworks(true);
            req->send(202, "text/plain", "Scan gestartet");
            return;
        }
        if (n == WIFI_SCAN_RUNNING) {
            req->send(202, "text/plain", "Scan läuft");
            return;
        }

        // Doppelte SSIDs (mehrere Access Points) zusammenfassen, stärkstes Signal behalten
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) continue;
            bool dup = false;
            for (JsonObject o : arr) {
                if (o["ssid"] == ssid) {
                    if (WiFi.RSSI(i) > o["rssi"].as<int>()) o["rssi"] = WiFi.RSSI(i);
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = ssid;
            o["rssi"] = WiFi.RSSI(i);
        }
        WiFi.scanDelete();

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: WLAN-Daten speichern → Neustart
    server.on("/api/saveWifi", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
        const char *body = collectBody(req, data, len, index, total);
        if (!body) return;

        JsonDocument doc;
        if (deserializeJson(doc, body, total) || !doc["ssid"].is<const char*>()) {
            req->send(400, "text/plain", "JSON Error");
            return;
        }
        String ssid = doc["ssid"].as<String>();
        if (ssid.isEmpty() || ssid.length() > 32) {
            req->send(400, "text/plain", "Ungültige SSID");
            return;
        }
        {
            StateLock lock;
            state.wifi_ssid = ssid;
            state.wifi_pass = doc["pass"] | "";
            saveConfig();
        }
        req->send(200, "text/plain", "OK");
        scheduleRestart(1500);
    });

    // API: WLAN-Daten löschen → Neustart im Access-Point-Modus
    server.on("/api/clearWifi", HTTP_POST, [](AsyncWebServerRequest *req){
        {
            StateLock lock;
            state.wifi_ssid = "";
            state.wifi_pass = "";
            saveConfig();
        }
        req->send(200, "text/plain", "OK");
        scheduleRestart(1500);
    });

    // Navigation & Default Pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/index.html", "text/html"); });
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/wifi.html", "text/html"); });
    server.on("/dmxconfig", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/dmxconfig.html", "text/html"); });
    server.on("/mh", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/moving_head.html", "text/html"); });

    setupOTA(); // Update-Seite /update + ArduinoOTA

    server.begin();
    Serial.println("Webserver mit allen MH & Audio Erweiterungen gestartet.");
}
