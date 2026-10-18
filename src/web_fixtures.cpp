// Fixtures & moving heads: DMX config, positions, channel test, find lamp
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

void registerFixtureRoutes() {
    // API: set an extra channel (gobo, color wheel, prism, focus) of a fixture
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

    // API: find lamp – fixture id flashes white for 5 s (id=-1 stops)
    server.on("/api/identify", HTTP_GET, [](AsyncWebServerRequest *req){
        int id = req->hasParam("id") ? req->getParam("id")->value().toInt() : -1;
        state.identifyIndex = id;
        state.identifyUntil = millis() + 5000;
        req->send(200, "text/plain", "OK");
    });

    // API: channel test – ch=1..512, v=0..255 (ends 2 min after the last change)
    server.on("/api/testChannel", HTTP_GET, [](AsyncWebServerRequest *req){
        int ch = req->hasParam("ch") ? req->getParam("ch")->value().toInt() : 0;
        if (ch < 1 || ch > 512 || !req->hasParam("v")) { req->send(400, "text/plain", "Error"); return; }
        state.testVal[ch - 1] = constrain(req->getParam("v")->value().toInt(), 0, 255);
        state.testMask[ch - 1] = true;
        state.testUntil = millis() + 120000;
        req->send(200, "text/plain", "OK");
    });

    server.on("/api/testClear", HTTP_GET, [](AsyncWebServerRequest *req){
        memset(state.testMask, 0, sizeof(state.testMask));
        state.testUntil = 0;
        req->send(200, "text/plain", "OK");
    });

    // --- Moving head positions ---
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

    // Save the current pan/tilt values of all moving heads as a position (same name = overwrite)
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

    // Recall position: move all stored heads there, BPM auto off for them
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

    // API: moving head list
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

    // API: joystick movement
    server.on("/api/move", HTTP_GET, [](AsyncWebServerRequest *req){
        if (req->hasParam("id") && req->hasParam("p") && req->hasParam("t")) {
            int id = req->getParam("id")->value().toInt();
            uint8_t p = req->getParam("p")->value().toInt();
            uint8_t t = req->getParam("t")->value().toInt();
            StateLock lock;
            if(id >= 0 && id < (int)state.fixtures.size()) {
                if (state.fixtures[id].isAutoMoving) {
                    state.fixtures[id].isAutoMoving = false; // joystick takes over
                    markConfigDirty();
                }
                setFixtureMovement(id, p, t);
                req->send(200, "text/plain", "OK");
                return;
            }
        }
        req->send(400, "text/plain", "Error");
    });

    // API: save pan/tilt corner points A/B
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

    // API: moving head selected → return current state
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

        // Extra channels: only those the fixture type has
        const FixtureDefinition *def = getFixtureDef(fx.type);
        JsonObject extras = doc["extras"].to<JsonObject>();
        for (int e = 0; e < EXTRA_COUNT; e++) {
            if (def && def->offsetExtra[e] >= 0) extras[EXTRA_NAMES[e]] = fx.extra[e];
        }
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: set auto mode explicitly (for the motor test)
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

    // API: toggle auto mode
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

    // API: get config
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
        sched["time"]   = currentTimeString();   // empty = no time yet
        doc["displayOffMin"] = state.displayOffMin;
        doc["autoShowSmart"] = state.autoShowSmart;
        JsonObject sync = doc["syncCfg"].to<JsonObject>();
        sync["role"]  = state.syncRole;
        sync["group"] = state.syncGroup;
        JsonObject mq = doc["mqttCfg"].to<JsonObject>();
        mq["on"]      = state.mqttOn;
        mq["host"]    = state.mqttHost;
        mq["port"]    = state.mqttPort;
        mq["user"]    = state.mqttUser;
        mq["hasPass"] = state.mqttPass.length() > 0;   // never send the password itself
        mq["topic"]   = state.mqttTopic;
        JsonObject au = doc["authCfg"].to<JsonObject>();
        au["on"]      = state.authOn;
        au["user"]    = state.authUser;
        au["hasPass"] = state.authPass.length() >= 4;   // never send the password itself
        JsonArray chases = doc["userChases"].to<JsonArray>();
        for (int c = 0; c < USER_CHASES; c++) chases.add(state.userChases[c].name);
        doc["ip"]           = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: save config
    // The body may arrive in several chunks → collect first, then parse
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

        // Remember old fixtures so MH limits & auto mode are kept
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
        memset(state.dmx, 0, sizeof(state.dmx)); // Clear old values at shifted addresses
        updateDMXChannelCount();                 // also re-applies gobo & co.

        // Flash writing and DMX restart are done by the effect task
        state.dmxReinit = true;
        configSaveNow = true;
        req->send(200, "text/plain", "OK");
    });
}
