#include "web.h"
#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

// Definition des Webservers
AsyncWebServer server(80);  

unsigned long lastChangeTime = 0;
bool configDirty = false;
extern void setupDMX();

void markConfigDirty() {
    lastChangeTime = millis();
    configDirty = true;
}

void handleAutoSave() {
    if (configDirty && (millis() - lastChangeTime > 5000)) {
        saveConfig();
        configDirty = false;
        Serial.println(">>> Flash: Auto-Save (Farbe/Master/MH-Bounds) erledigt.");
    }
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
        doc["beat"]   = state.beat;
        doc["drop"]   = state.drop;
        doc["bpm"]    = (isnan(state.bpm) || state.bpm < 10) ? 0 : (int)state.bpm;
        doc["master"] = state.master;
        doc["gain"]   = state.gain;
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
            if(id >= 0 && id < (int)state.fixtures.size()) {
                state.fixtures[id].isAutoMoving = false; 
                setFixtureMovement(id, p, t);
                req->send(200, "text/plain", "OK");
            }
        } else { req->send(400, "text/plain", "Error"); }
    });

    // API: Pan/Tilt Eckpunkte A/B speichern
    server.on("/api/save", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("id") && req->hasParam("pt")) {
            int id = req->getParam("id")->value().toInt();
            String pt = req->getParam("pt")->value();
            if(id >= 0 && id < (int)state.fixtures.size()) {
                auto &fx = state.fixtures[id];
                const FixtureDefinition* def = getFixtureDef(fx.type);
                int base = fx.start - 1;
                if(def && def->offsetPan >= 0) {
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

    // API: Auto-Modus Toggle
    server.on("/api/toggleAuto", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("id")) {
            int id = req->getParam("id")->value().toInt();
            if(id >= 0 && id < (int)state.fixtures.size()) {
                state.fixtures[id].isAutoMoving = !state.fixtures[id].isAutoMoving;
                req->send(200, "text/plain", state.fixtures[id].isAutoMoving ? "ON" : "OFF");
            }
        }
    });

    // API: Standard DMX Steuerung
    server.on("/api/setColor", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("hex")) {
            hexToRGB(req->getParam("hex")->value(), state.colorR, state.colorG, state.colorB);
            state.currentEffect = "static"; 
            markConfigDirty();
        }
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/setMaster", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("value")) {
            state.master = req->getParam("value")->value().toInt();
            markConfigDirty();
        }
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/setEffect", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("name")) {
            state.currentEffect = req->getParam("name")->value();
            markConfigDirty();
        }
        req->send(200, "text/plain", "OK");
    });
    // API: Effekt-Geschwindigkeit (Timer) setzen
    server.on("/api/setTimer", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("ms")) {
            state.timerSpeed = req->getParam("ms")->value().toInt();
            // Damit der Wert nach Neustart bleibt:
            markConfigDirty(); 
            req->send(200, "text/plain", "OK");
        } else {
            req->send(400, "text/plain", "Parameter 'ms' fehlt");
        }
    });


    // API: Config abrufen
    server.on("/api/getConfig", HTTP_GET, [](AsyncWebServerRequest *req){
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
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: Config speichern (PATCH)
    server.on("/api/saveConfig", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t){
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
            req->send(400, "text/plain", "JSON Error");
            return;
        }
        state.fixtures.clear();
        for (JsonObject o : doc["fixtures"].as<JsonArray>()) {
            Fixture fx;
            fx.name = o["name"].as<String>();
            fx.start = o["start"].as<int>();
            fx.type = o["type"].as<String>();
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
        saveConfig();
        updateDMXChannelCount();
        setupDMX();
        req->send(200, "text/plain", "OK");
    });

    // Navigation & Default Pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/index.html", "text/html"); });
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/wifi.html", "text/html"); });
    server.on("/dmxconfig", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/dmxconfig.html", "text/html"); });
    server.on("/mh", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/moving_head.html", "text/html"); });

    server.begin();
    Serial.println("Webserver mit allen MH & Audio Erweiterungen gestartet.");
}
