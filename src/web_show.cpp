// Show-Steuerung: Effekte, Farben, Presets, Gruppen, Cue-Liste, Lauflichter, Einstellungen
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

void registerShowRoutes() {
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

        // Intelligente Auto-Show, Sync
        if ((v = param("autoShowSmart")).length())  state.autoShowSmart = v.toInt() != 0;
        if ((v = param("syncRole")).length())       state.syncRole = constrain(v.toInt(), 0, 2);
        if ((v = param("syncGroup")).length())      state.syncGroup = constrain(v.toInt(), 1, 9);

        // MQTT (Passwort nur ändern, wenn mitgeschickt)
        bool mqttChanged = false;
        if ((v = param("mqttOn")).length())         { state.mqttOn = v.toInt() != 0; mqttChanged = true; }
        if (req->hasParam("mqttHost"))              { state.mqttHost = param("mqttHost"); mqttChanged = true; }
        if ((v = param("mqttPort")).length())       { state.mqttPort = constrain(v.toInt(), 1, 65535); mqttChanged = true; }
        if (req->hasParam("mqttUser"))              { state.mqttUser = param("mqttUser"); mqttChanged = true; }
        if (req->hasParam("mqttPass"))              { state.mqttPass = param("mqttPass"); mqttChanged = true; }
        if ((v = param("mqttTopic")).length())      { state.mqttTopic = v; mqttChanged = true; }
        if (mqttChanged) mqttReconfigure();

        // Passwortschutz (Passwort nur ändern, wenn mitgeschickt; mind. 4 Zeichen)
        if (req->hasParam("authUser") && param("authUser").length()) state.authUser = param("authUser").substring(0, 20);
        if (req->hasParam("authPass")) {
            String p = param("authPass");
            if (p.length() >= 4) state.authPass = p.substring(0, 32);
        }
        if ((v = param("authOn")).length()) state.authOn = v.toInt() != 0;

        bool netChanged = false;
        if ((v = param("netInput")).length())    { state.netInput = v.toInt() != 0; netChanged = true; }
        if ((v = param("netUniverse")).length()) { state.netUniverse = constrain(v.toInt(), 1, 32768); netChanged = true; }
        if (netChanged) setupNetInput();

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

    // API: Energie eines Presets für die intelligente Auto-Show (0 egal, 1 ruhig, 2 mittel, 3 wild)
    server.on("/api/presetEnergy", HTTP_GET, [](AsyncWebServerRequest *req){
        int id = req->hasParam("id") ? req->getParam("id")->value().toInt() : -1;
        int e = req->hasParam("e") ? req->getParam("e")->value().toInt() : 0;
        StateLock lock;
        if (id < 0 || id >= (int)state.presets.size()) { req->send(400, "text/plain", "Error"); return; }
        state.presets[id].energy = constrain(e, 0, 3);
        markConfigDirty();
        req->send(200, "text/plain", "OK");
    });

    // --- Eigene Lauflichter ---
    server.on("/api/chases", HTTP_GET, [](AsyncWebServerRequest *req){
        StateLock lock;
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int c = 0; c < USER_CHASES; c++) {
            const UserChase &uc = state.userChases[c];
            JsonObject o = arr.add<JsonObject>();
            o["name"]   = uc.name;
            o["width"]  = uc.width;
            o["steps"]  = uc.steps;
            o["smooth"] = uc.smooth;
            JsonArray cells = o["cells"].to<JsonArray>();
            for (int st = 0; st < USER_CHASE_STEPS; st++)
                for (int x = 0; x < USER_CHASE_WIDTH; x++) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "#%06lx", (unsigned long)(uc.cells[st][x] & 0xFFFFFF));
                    cells.add(hex);
                }
        }
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // Lauflicht speichern: slot=1..4, Body {name, width, steps, smooth, cells:[16×8 "#rrggbb"]}
    server.on("/api/saveChase", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
        const char *body = collectBody(req, data, len, index, total);
        if (!body) return;
        int slot = req->hasParam("slot") ? req->getParam("slot")->value().toInt() - 1 : -1;
        JsonDocument doc;
        if (slot < 0 || slot >= USER_CHASES || deserializeJson(doc, body, total)) {
            req->send(400, "text/plain", "Fehler");
            return;
        }
        StateLock lock;
        UserChase &uc = state.userChases[slot];
        uc = UserChase();
        uc.name   = String(doc["name"] | "").substring(0, 20);
        uc.width  = constrain((int)(doc["width"] | 4), 1, USER_CHASE_WIDTH);
        uc.steps  = constrain((int)(doc["steps"] | 4), 1, USER_CHASE_STEPS);
        uc.smooth = doc["smooth"] | false;
        int i = 0;
        for (JsonVariant v : doc["cells"].as<JsonArray>()) {
            if (i >= USER_CHASE_STEPS * USER_CHASE_WIDTH) break;
            String hex = v.as<String>();
            if (hex.startsWith("#")) hex = hex.substring(1);
            uc.cells[i / USER_CHASE_WIDTH][i % USER_CHASE_WIDTH] = strtoul(hex.c_str(), NULL, 16);
            i++;
        }
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
}
