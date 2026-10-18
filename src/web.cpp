#include "web.h"
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

// Definition des Webservers
AsyncWebServer server(80);  
AsyncWebSocket ws("/ws");   // Live-Status (Pegel, Beat, Drop, BPM) an die Browser

volatile unsigned long lastChangeTime = 0;
volatile bool configDirty = false;
volatile bool configSaveNow = false;

// Max. Größe für /api/saveConfig (schützt vor zu großen Requests)
#define MAX_CONFIG_BODY 16384

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
static void presetToJson(JsonArray arr, int id, const Preset &p) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]     = id;
    o["name"]   = p.name;
    o["effect"] = p.effect;
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", p.r, p.g, p.b);
    o["color"]  = hex;
    o["speed"]  = p.timerSpeed;
    o["master"] = p.master;
}

void setupWeb() {
    // --- Statische Dateien (Lokal vom ESP32) ---
    server.serveStatic("/moving_head.html", LittleFS, "/moving_head.html");
    server.serveStatic("/nipplejs.min.js", LittleFS, "/nipplejs.min.js");
    server.serveStatic("/style.css", LittleFS, "/style.css");
    server.serveStatic("/logo.svg", LittleFS, "/logo.svg");

    // API: Live Status (inkl. Audio-Daten für UI)
    // (Fallback, falls der WebSocket nicht verbunden ist)
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req){
        bool beat = (millis() - state.lastBeatMs) < 150; // kurz "halten", damit die UI ihn sieht
        req->send(200, "application/json", buildStatusJson(beat));
    });

    // WebSocket für den Live-Status
    server.addHandler(&ws);

    // API: Blackout an/aus (wirkt sofort, wird nicht gespeichert)
    server.on("/api/blackout", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("on")) {
            state.blackout = req->getParam("on")->value().toInt() != 0;
            req->send(200, "text/plain", state.blackout ? "ON" : "OFF");
        } else { req->send(400, "text/plain", "Error"); }
    });

    // API: Tap-Tempo. Mehrmals im Takt tippen → BPM wird übernommen und festgehalten
    server.on("/api/tap", HTTP_GET, [](AsyncWebServerRequest *req){
        state.lastUserMs = millis();
        req->send(200, "text/plain", String(registerTap()));
    });

    // API: BPM festhalten an/aus (aus = wieder Erkennung aus der Musik)
    server.on("/api/bpmLock", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("on")) {
            state.bpmLocked = req->getParam("on")->value().toInt() != 0;
            if (state.bpmLocked) {
                if (state.bpm < 40) state.bpm = 120;
                state.clockSyncMs = millis();
            }
            req->send(200, "text/plain", state.bpmLocked ? "ON" : "OFF");
        } else { req->send(400, "text/plain", "Error"); }
    });

    // API: Einstellungen (beliebige Kombination der Parameter)
    // beatSync=0|1, stepsPerBeat=0.5|1|2, autoShow=0|1, showBeats=N,
    // dropAction=0..3, dropPreset=Name, movePattern=0..4, moveSpread=0|1
    server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req){
        StateLock lock;
        auto param = [req](const char *name) -> String {
            return req->hasParam(name) ? req->getParam(name)->value() : String();
        };
        String v;
        if ((v = param("beatSync")).length())     state.beatSync = v.toInt() != 0;
        if ((v = param("stepsPerBeat")).length()) {
            float f = v.toFloat();
            state.stepsPerBeat = (f == 2.0f || f == 0.5f) ? f : 1.0f;
        }
        if ((v = param("autoShow")).length())     state.autoShow = v.toInt() != 0;
        if ((v = param("showBeats")).length())    state.showBeats = constrain(v.toInt(), 4, 256);
        if ((v = param("dropAction")).length())   state.dropAction = constrain(v.toInt(), 0, DROP_ACTION_COUNT - 1);
        if (req->hasParam("dropPreset"))          state.dropPreset = param("dropPreset");
        if ((v = param("movePattern")).length())  state.movePattern = constrain(v.toInt(), 0, MOVE_PATTERN_COUNT - 1);
        if ((v = param("moveSpread")).length())   state.moveSpread = v.toInt() != 0;

        // Palette: paletteOn=0|1, paletteSize=2..4, palette=#rrggbb,#rrggbb,…
        if ((v = param("paletteOn")).length())   state.palette.on = v.toInt() != 0;
        if ((v = param("paletteSize")).length()) state.palette.size = constrain(v.toInt(), 2, PALETTE_MAX);
        if ((v = param("palette")).length()) {
            int start = 0;
            for (int i = 0; i < PALETTE_MAX && start < (int)v.length(); i++) {
                int comma = v.indexOf(',', start);
                if (comma < 0) comma = v.length();
                hexToColor(v.substring(start, comma), state.palette.c[i][0], state.palette.c[i][1], state.palette.c[i][2]);
                start = comma + 1;
            }
        }

        // Nebel
        if ((v = param("fogChannel")).length())     state.fogChannel = constrain(v.toInt(), 0, 512);
        if ((v = param("fogLevel")).length())       state.fogLevel = constrain(v.toInt(), 0, 255);
        if ((v = param("fogOnDrop")).length())      state.fogOnDrop = v.toInt() != 0;
        if ((v = param("fogDropSec")).length())     state.fogDropSec = constrain(v.toInt(), 1, 30);
        if ((v = param("fogIntervalMin")).length()) state.fogIntervalMin = constrain(v.toInt(), 0, 120);
        if ((v = param("fogIntervalSec")).length()) state.fogIntervalSec = constrain(v.toInt(), 1, 30);

        // Standby
        if ((v = param("standbyMin")).length())     state.standbyMin = constrain(v.toInt(), 0, 240);
        if (req->hasParam("standbyPreset"))         state.standbyPreset = param("standbyPreset");

        // Cue-Liste
        if ((v = param("cueLoop")).length())        state.cueLoop = v.toInt() != 0;

        // Übergänge, Effekt-Parameter, Lautstärke-Helligkeit
        if ((v = param("fadeMs")).length())         state.fadeMs = constrain(v.toInt(), 0, 10000);
        if ((v = param("fxWidth")).length())        state.fxWidth = constrain(v.toInt(), 1, 8);
        if ((v = param("fxIntensity")).length())    state.fxIntensity = constrain(v.toInt(), 0, 100);
        if ((v = param("levelDim")).length())       state.levelDim = v.toInt() != 0;
        if ((v = param("levelDimDepth")).length())  state.levelDimDepth = constrain(v.toInt(), 0, 100);

        // Zeitplan, Display
        if ((v = param("scheduleOn")).length())     state.scheduleOn = v.toInt() != 0;
        if ((v = param("scheduleStart")).length())  state.scheduleStart = v.substring(0, 5);
        if ((v = param("scheduleEnd")).length())    state.scheduleEnd = v.substring(0, 5);
        if (req->hasParam("schedulePreset"))        state.schedulePreset = param("schedulePreset");
        if ((v = param("displayOffMin")).length())  state.displayOffMin = constrain(v.toInt(), 0, 240);

        bool netChanged = false;
        if ((v = param("netInput")).length())    { state.netInput = v.toInt() != 0; netChanged = true; }
        if ((v = param("netUniverse")).length()) { state.netUniverse = constrain(v.toInt(), 1, 32768); netChanged = true; }
        if (netChanged) setupNetInput();

        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    // API: Zusatzkanal (Gobo, Farbrad, Prisma, Fokus) eines Fixtures setzen
    server.on("/api/setExtra", HTTP_GET, [](AsyncWebServerRequest *req){
        if (!req->hasParam("id") || !req->hasParam("ch") || !req->hasParam("v")) {
            req->send(400, "text/plain", "Error");
            return;
        }
        int id = req->getParam("id")->value().toInt();
        String ch = req->getParam("ch")->value();
        int v = constrain(req->getParam("v")->value().toInt(), 0, 255);

        int e = -1;
        for (int i = 0; i < EXTRA_COUNT; i++) if (ch == EXTRA_NAMES[i]) e = i;

        StateLock lock;
        if (e < 0 || id < 0 || id >= (int)state.fixtures.size()) {
            req->send(400, "text/plain", "Error");
            return;
        }
        state.fixtures[id].extra[e] = v;
        applyFixtureExtras();
        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    // API: Gruppe einstellen. g=1..4, custom=0|1, effect=Name, color=#rrggbb, name=Text
    server.on("/api/setGroup", HTTP_GET, [](AsyncWebServerRequest *req){
        int g = req->hasParam("g") ? req->getParam("g")->value().toInt() : 0;
        if (g < 1 || g > GROUP_COUNT) { req->send(400, "text/plain", "Error"); return; }
        StateLock lock;
        GroupSettings &G = state.groups[g - 1];
        if (req->hasParam("custom")) G.custom = req->getParam("custom")->value().toInt() != 0;
        if (req->hasParam("effect")) G.effect = req->getParam("effect")->value();
        if (req->hasParam("color"))  hexToColor(req->getParam("color")->value(), G.r, G.g, G.b);
        if (req->hasParam("name"))   G.name = req->getParam("name")->value().substring(0, 20);
        state.fadeRequest = true;
        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    // API: Nebel-Knopf (wie Strobe: solange gedrückt wiederholt on=1 senden)
    server.on("/api/fog", HTTP_GET, [](AsyncWebServerRequest *req){
        const unsigned long FOG_KEEPALIVE_MS = 600;
        if (req->hasParam("on")) {
            bool on = req->getParam("on")->value().toInt() != 0;
            state.manualFogUntil = on ? millis() + FOG_KEEPALIVE_MS : 0;
            req->send(200, "text/plain", "OK");
        } else { req->send(400, "text/plain", "Error"); }
    });

    // API: Zeitplan "aus" überstimmen (Licht jetzt an, bis zum nächsten Wechsel)
    server.on("/api/scheduleOverride", HTTP_GET, [](AsyncWebServerRequest *req){
        state.scheduleOverride = !req->hasParam("on") || req->getParam("on")->value().toInt() != 0;
        req->send(200, "text/plain", "OK");
    });

    // --- Moving-Head-Positionen ---
    server.on("/api/positions", HTTP_GET, [](AsyncWebServerRequest *req){
        StateLock lock;
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (size_t i = 0; i < state.positions.size(); i++) {
            JsonObject o = arr.add<JsonObject>();
            o["id"]    = i;
            o["name"]  = state.positions[i].name;
            o["heads"] = state.positions[i].heads.size();
        }
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // Aktuelle Pan/Tilt-Werte aller Moving Heads als Position speichern (gleicher Name = überschreiben)
    server.on("/api/savePosition", HTTP_GET, [](AsyncWebServerRequest *req){
        String name = req->hasParam("name") ? req->getParam("name")->value() : "";
        name.trim();
        if (name.isEmpty() || name.length() > PRESET_NAME_MAX) {
            req->send(400, "text/plain", "Name fehlt oder ist zu lang");
            return;
        }
        StateLock lock;
        MhPosition pos;
        pos.name = name;
        for (auto &fx : state.fixtures) {
            const FixtureDefinition *def = getFixtureDef(fx.type);
            if (!def || def->offsetPan < 0) continue;
            int base = fx.start - 1;
            int p = base + def->offsetPan, t = base + def->offsetTilt;
            pos.heads.push_back({fx.name,
                                 (uint8_t)(p >= 0 && p < 512 ? state.dmx[p] : 128),
                                 (uint8_t)(def->offsetTilt >= 0 && t < 512 ? state.dmx[t] : 128)});
        }
        if (pos.heads.empty()) {
            req->send(400, "text/plain", "Keine Moving Heads angelegt");
            return;
        }
        int idx = -1;
        for (size_t i = 0; i < state.positions.size(); i++) if (state.positions[i].name == name) idx = i;
        if (idx >= 0) state.positions[idx] = pos;
        else if (state.positions.size() < MAX_POSITIONS) state.positions.push_back(pos);
        else { req->send(409, "text/plain", "Maximal " + String(MAX_POSITIONS) + " Positionen"); return; }
        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    // Position anfahren: alle gespeicherten Heads dorthin, BPM-Auto für sie aus
    server.on("/api/loadPosition", HTTP_GET, [](AsyncWebServerRequest *req){
        int id = req->hasParam("id") ? req->getParam("id")->value().toInt() : -1;
        StateLock lock;
        if (id < 0 || id >= (int)state.positions.size()) { req->send(400, "text/plain", "Error"); return; }
        for (auto &h : state.positions[id].heads) {
            for (size_t i = 0; i < state.fixtures.size(); i++) {
                if (state.fixtures[i].name != h.fixture) continue;
                state.fixtures[i].isAutoMoving = false;
                setFixtureMovement(i, h.pan, h.tilt);
            }
        }
        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/deletePosition", HTTP_GET, [](AsyncWebServerRequest *req){
        int id = req->hasParam("id") ? req->getParam("id")->value().toInt() : -1;
        StateLock lock;
        if (id < 0 || id >= (int)state.positions.size()) { req->send(400, "text/plain", "Error"); return; }
        state.positions.erase(state.positions.begin() + id);
        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    // --- Cue-Liste ---
    server.on("/api/cues", HTTP_GET, [](AsyncWebServerRequest *req){
        StateLock lock;
        JsonDocument doc;
        JsonArray arr = doc["cues"].to<JsonArray>();
        for (auto &c : state.cues) {
            JsonObject o = arr.add<JsonObject>();
            o["preset"] = c.preset;
            o["dur"]    = c.duration;
            o["beats"]  = c.beats;
        }
        doc["loop"]    = state.cueLoop;
        doc["running"] = state.cueRunning;
        doc["index"]   = state.cueIndex;
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    server.on("/api/saveCues", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
        const char *body = collectBody(req, data, len, index, total);
        if (!body) return;
        JsonDocument doc;
        if (deserializeJson(doc, body, total) || !doc.is<JsonArray>()) {
            req->send(400, "text/plain", "JSON Error");
            return;
        }
        StateLock lock;
        state.cues.clear();
        for (JsonObject o : doc.as<JsonArray>()) {
            if (state.cues.size() >= MAX_CUES) break;
            Cue c;
            c.preset   = o["preset"] | "";
            c.duration = constrain((int)(o["dur"] | 16), 1, 9999);
            c.beats    = o["beats"] | true;
            state.cues.push_back(c);
        }
        if (state.cueIndex >= (int)state.cues.size()) state.cueIndex = state.cues.size() - 1;
        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    // API: Cue-Liste steuern: action=start|stop|next
    server.on("/api/cue", HTTP_GET, [](AsyncWebServerRequest *req){
        String action = req->hasParam("action") ? req->getParam("action")->value() : "";
        StateLock lock;
        if (action == "start" && !state.cues.empty()) {
            state.autoShow = false;   // Auto-Show und Cue-Liste schließen sich aus
            state.cueRunning = true;
            state.cueRestart = true;
        } else if (action == "stop") {
            state.cueRunning = false;
        } else if (action == "next" && state.cueRunning) {
            state.cueNext = true;
        } else {
            req->send(400, "text/plain", "Error");
            return;
        }
        req->send(200, "text/plain", "OK");
    });

    // API: Mikrofon einmessen. step=silence|music|apply|cancel; ohne step: Status
    server.on("/api/calibrate", HTTP_GET, [](AsyncWebServerRequest *req){
        String step = req->hasParam("step") ? req->getParam("step")->value() : "";
        String message;
        bool ok = true;
        if (step == "silence")     startCalibration(CAL_SILENCE, 3000);
        else if (step == "music")  startCalibration(CAL_MUSIC, 8000);
        else if (step == "cancel") cancelCalibration();
        else if (step == "apply") {
            StateLock lock;
            ok = applyCalibration(message);
        }
        JsonDocument doc;
        calibrationStatus(doc.to<JsonObject>());
        doc["ok"] = ok;
        if (message.length()) doc["message"] = message;
        String json;
        serializeJson(doc, json);
        req->send(ok ? 200 : 400, "application/json", json);
    });

    // --- Presets ---
    server.on("/api/presets", HTTP_GET, [](AsyncWebServerRequest *req){
        StateLock lock;
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (size_t i = 0; i < state.presets.size(); i++) presetToJson(arr, i, state.presets[i]);
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // Aktuelle Einstellungen als Preset speichern (gleicher Name = überschreiben)
    server.on("/api/savePreset", HTTP_GET, [](AsyncWebServerRequest *req){
        String name = req->hasParam("name") ? req->getParam("name")->value() : "";
        name.trim();
        if (name.isEmpty() || name.length() > PRESET_NAME_MAX) {
            req->send(400, "text/plain", "Name fehlt oder ist zu lang");
            return;
        }
        StateLock lock;
        Preset *target = nullptr;
        for (auto &p : state.presets) if (p.name == name) target = &p;
        if (!target) {
            if (state.presets.size() >= MAX_PRESETS) {
                req->send(409, "text/plain", "Maximal " + String(MAX_PRESETS) + " Presets");
                return;
            }
            state.presets.push_back(Preset());
            target = &state.presets.back();
            target->name = name;
        }
        *target = capturePreset(name); // inkl. Gruppen und Palette
        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/loadPreset", HTTP_GET, [](AsyncWebServerRequest *req){
        int id = req->hasParam("id") ? req->getParam("id")->value().toInt() : -1;
        StateLock lock;
        if (id < 0 || id >= (int)state.presets.size()) {
            req->send(400, "text/plain", "Error");
            return;
        }
        applyPreset(id, true);
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/deletePreset", HTTP_GET, [](AsyncWebServerRequest *req){
        int id = req->hasParam("id") ? req->getParam("id")->value().toInt() : -1;
        StateLock lock;
        if (id < 0 || id >= (int)state.presets.size()) {
            req->send(400, "text/plain", "Error");
            return;
        }
        state.presets.erase(state.presets.begin() + id);
        if (state.activePreset == id) state.activePreset = -1;
        else if (state.activePreset > id) state.activePreset = state.activePreset - 1;
        markConfigDirty();
        req->send(200, "text/plain", "OK");
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

        // Zusatzkanäle: nur die, die der Fixture-Typ hat
        const FixtureDefinition *def = getFixtureDef(fx.type);
        JsonObject extras = doc["extras"].to<JsonObject>();
        for (int e = 0; e < EXTRA_COUNT; e++) {
            if (def && def->offsetExtra[e] >= 0) extras[EXTRA_NAMES[e]] = fx.extra[e];
        }
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
            state.activePreset = -1;   // von Hand geändert
            state.fadeRequest = true;
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
            if (state.currentEffect != name) state.fadeRequest = true;
            state.currentEffect = name;
            state.activePreset = -1;   // von Hand geändert
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
            o["group"] = fx.group;
        }
        JsonObject types = doc["fixtureTypes"].to<JsonObject>();
        for (auto &kv : state.fixtureTypes) {
            fixtureDefToJson(types[kv.first].to<JsonObject>(), kv.second);
        }
        doc["master"] = state.master;
        doc["effect"] = state.currentEffect;
        doc["audioThreshold"] = state.audioThreshold;
        doc["dropEnabled"] = state.dropEnabled;
        doc["timerSpeed"] = state.timerSpeed;
        char hex[8];
        snprintf(hex, sizeof(hex), "#%02x%02x%02x", state.colorR, state.colorG, state.colorB);
        doc["color"] = hex;
        doc["dropAction"]   = state.dropAction;
        doc["dropPreset"]   = state.dropPreset;
        doc["beatSync"]     = state.beatSync;
        doc["stepsPerBeat"] = state.stepsPerBeat;
        doc["movePattern"]  = state.movePattern;
        doc["moveSpread"]   = state.moveSpread;
        doc["autoShow"]     = state.autoShow;
        doc["showBeats"]    = state.showBeats;
        doc["netInput"]     = state.netInput;
        doc["netUniverse"]  = state.netUniverse;
        groupsToJsonPublic(doc["groups"].to<JsonArray>());
        paletteToJsonPublic(doc["palette"].to<JsonObject>());
        JsonObject fog = doc["fog"].to<JsonObject>();
        fog["channel"]     = state.fogChannel;
        fog["level"]       = state.fogLevel;
        fog["onDrop"]      = state.fogOnDrop;
        fog["dropSec"]     = state.fogDropSec;
        fog["intervalMin"] = state.fogIntervalMin;
        fog["intervalSec"] = state.fogIntervalSec;
        doc["standbyMin"]    = state.standbyMin;
        doc["standbyPreset"] = state.standbyPreset;
        doc["cueLoop"]       = state.cueLoop;
        doc["calibrated"]    = state.calibrated;
        doc["fadeMs"]        = state.fadeMs;
        doc["fxWidth"]       = state.fxWidth;
        doc["fxIntensity"]   = state.fxIntensity;
        doc["levelDim"]      = state.levelDim;
        doc["levelDimDepth"] = state.levelDimDepth;
        JsonObject sched = doc["schedule"].to<JsonObject>();
        sched["on"]     = state.scheduleOn;
        sched["start"]  = state.scheduleStart;
        sched["end"]    = state.scheduleEnd;
        sched["preset"] = state.schedulePreset;
        sched["time"]   = currentTimeString();   // leer = noch keine Uhrzeit
        doc["displayOffMin"] = state.displayOffMin;
        doc["ip"]           = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
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
            fx.group = constrain((int)(o["group"] | 0), 0, GROUP_COUNT);
            for (auto &old : oldFixtures) {
                if (old.name == fx.name) {
                    fx.panMin = old.panMin;   fx.panMax = old.panMax;
                    fx.tiltMin = old.tiltMin; fx.tiltMax = old.tiltMax;
                    fx.isAutoMoving = old.isAutoMoving;
                    memcpy(fx.extra, old.extra, sizeof(fx.extra));
                    break;
                }
            }
            state.fixtures.push_back(fx);
        }
        state.fixtureTypes.clear();
        for (JsonPair kv : doc["fixtureTypes"].as<JsonObject>()) {
            state.fixtureTypes[kv.key().c_str()] = fixtureDefFromJson(kv.value().as<JsonObject>());
        }
        memset(state.dmx, 0, sizeof(state.dmx)); // Alte Werte auf verschobenen Adressen löschen
        updateDMXChannelCount();                 // setzt auch Gobo & Co. wieder

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

    // API: Auslastung der Tasks (zum Prüfen der Aufteilung auf die Cores)
    server.on("/api/load", HTTP_GET, [](AsyncWebServerRequest *req){
        static uint32_t lastFrames = 0;
        static unsigned long lastMs = 0;
        unsigned long now = millis();
        uint32_t frames = loadStats.dmxFrames;
        float fps = (lastMs && now > lastMs) ? (frames - lastFrames) * 1000.0f / (now - lastMs) : 0;
        lastFrames = frames;
        lastMs = now;

        JsonDocument doc;
        // Audio: Anteil der Rechenzeit an einem Mikrofon-Frame (1024 Samples = 23,2 ms)
        JsonObject a = doc["audio"].to<JsonObject>();
        a["core"] = CORE_AUDIO;
        a["us"] = loadStats.audioUs;
        a["maxUs"] = loadStats.audioMaxUs;
        a["percent"] = loadStats.audioUs * 100 / 23220;
        JsonObject e = doc["effect"].to<JsonObject>();
        e["core"] = CORE_LIGHT;
        e["us"] = loadStats.effectUs;
        e["maxUs"] = loadStats.effectMaxUs;
        JsonObject d = doc["dmx"].to<JsonObject>();
        d["core"] = CORE_LIGHT;
        d["us"] = loadStats.dmxUs;
        d["maxUs"] = loadStats.dmxMaxUs;
        d["fps"] = roundf(fps * 10) / 10;
        JsonObject disp = doc["display"].to<JsonObject>();
        disp["core"] = CORE_AUDIO;
        disp["us"] = loadStats.displayUs;

        // Freier Stack pro Task (Bytes) – zeigt, ob die Stack-Größen reichen
        JsonObject st = doc["stackFree"].to<JsonObject>();
        if (loadStats.audioTask)   st["audio"]   = uxTaskGetStackHighWaterMark(loadStats.audioTask);
        if (loadStats.effectTask)  st["effect"]  = uxTaskGetStackHighWaterMark(loadStats.effectTask);
        if (loadStats.dmxTask)     st["dmx"]     = uxTaskGetStackHighWaterMark(loadStats.dmxTask);
        if (loadStats.displayTask) st["display"] = uxTaskGetStackHighWaterMark(loadStats.displayTask);

        doc["heapFree"] = ESP.getFreeHeap();
        doc["heapMin"]  = ESP.getMinFreeHeap();
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: Aktuell gesendete DMX-Werte (binär, 1 Byte pro Kanal) für den DMX-Monitor
    server.on("/api/dmx", HTTP_GET, [](AsyncWebServerRequest *req){
        static uint8_t buf[512];
        int n;
        {
            StateLock lock;
            n = getDMXOutput(buf, sizeof(buf));
        }
        AsyncWebServerResponse *res = req->beginResponse(200, "application/octet-stream", buf, n);
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
    });

    // API: Backup herunterladen (ohne WLAN-Passwort; Login wie bei /update)
    server.on("/api/backup", HTTP_GET, [](AsyncWebServerRequest *req){
        if (!req->authenticate(OTA_USER, OTA_PASSWORD)) return req->requestAuthentication();
        String json;
        {
            StateLock lock;
            json = exportConfigJson();
        }
        AsyncWebServerResponse *res = req->beginResponse(200, "application/json", json);
        res->addHeader("Content-Disposition", "attachment; filename=\"dmx-backup.json\"");
        req->send(res);
    });

    // API: Backup wiederherstellen → Neustart
    server.on("/api/restore", HTTP_POST,
        [](AsyncWebServerRequest *req){
            // Ohne Login wurde der Body ignoriert → Anmeldung anfordern
            if (!req->authenticate(OTA_USER, OTA_PASSWORD)) req->requestAuthentication();
        }, NULL,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
            if (!req->authenticate(OTA_USER, OTA_PASSWORD)) return;
            const char *body = collectBody(req, data, len, index, total);
            if (!body) return;

            String error;
            bool ok;
            {
                StateLock lock;
                ok = importConfigJson(body, total, error);
            }
            if (!ok) {
                req->send(400, "text/plain", error);
                return;
            }
            req->send(200, "text/plain", "OK");
            scheduleRestart(1500);
        });

    // Navigation & Default Pages
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/index.html", "text/html"); });
    server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/wifi.html", "text/html"); });
    server.on("/dmxconfig", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/dmxconfig.html", "text/html"); });
    server.on("/mh", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/moving_head.html", "text/html"); });
    server.on("/monitor", HTTP_GET, [](AsyncWebServerRequest *req){ req->send(LittleFS, "/monitor.html", "text/html"); });

    setupOTA(); // Update-Seite /update + ArduinoOTA

    server.begin();
    Serial.println("Webserver mit allen MH & Audio Erweiterungen gestartet.");
}
