#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

ControllerState state;

// ----------------------------------------------------
// Default Fixture-Typen laden
// ----------------------------------------------------
void loadDefaultFixtureTypes() {
    state.fixtureTypes.clear();

    state.fixtureTypes["RGB3"]  = {3, -1, 0, 1, 2, -1, -1, -1};
    state.fixtureTypes["RGBW4"] = {4, -1, 0, 1, 2, 3, -1, -1};
    state.fixtureTypes["PAR7"]  = {7, 0, 1, 2, 3, 4, -1, -1};
    state.fixtureTypes["MH14"]  = {14, 6, 7, 8, 9, 10, 0, 1};
    state.fixtureTypes["DRGB"]  = {7, 0, 1, 2, 3, -1, -1, 1};
}

// ----------------------------------------------------
// FixtureDefinition holen
// ----------------------------------------------------
const FixtureDefinition* getFixtureDef(const String &name) {
    auto it = state.fixtureTypes.find(name);
    if (it == state.fixtureTypes.end()) return nullptr;
    return &it->second;
}

// ----------------------------------------------------
// Startadresse eines Fixtures (1-basiert)
// ----------------------------------------------------
int getFixtureStartAddress(int fixtureIndex) {
    if (fixtureIndex < 0 || fixtureIndex >= (int)state.fixtures.size())
        return 1;
    return state.fixtures[fixtureIndex].start;
}

// ----------------------------------------------------
// DMX Kanalanzahl berechnen
// ----------------------------------------------------
void updateDMXChannelCount() {
    int maxChannel = 0;
    for (auto &fx : state.fixtures) {
        const FixtureDefinition *def = getFixtureDef(fx.type);
        if (!def) continue;
        int end = fx.start + def->channels - 1;
        if (end > maxChannel) maxChannel = end;
    }
    state.numChannels = maxChannel;
    if (state.numChannels > 512) state.numChannels = 512;
}

// ----------------------------------------------------
// Config speichern (V7 Syntax)
// ----------------------------------------------------
void saveConfig() {
    JsonDocument doc;

    // Fixtures speichern inkl. MH-Grenzen
    JsonArray arr = doc["fixtures"].to<JsonArray>();
    for (auto &fx : state.fixtures) {
        JsonObject o = arr.add<JsonObject>();
        o["name"]  = fx.name;
        o["start"] = fx.start;
        o["type"]  = fx.type;
        o["pMin"]  = fx.panMin;
        o["pMax"]  = fx.panMax;
        o["tMin"]  = fx.tiltMin;
        o["tMax"]  = fx.tiltMax;
        o["auto"]  = fx.isAutoMoving;
    }

    // Fixture-Typen speichern
    JsonObject types = doc["fixtureTypes"].to<JsonObject>();
    for (auto &kv : state.fixtureTypes) {
        JsonObject t = types[kv.first].to<JsonObject>();
        t["channels"]     = kv.second.channels;
        t["offsetDimmer"] = kv.second.offsetDimmer;
        t["offsetR"]      = kv.second.offsetR;
        t["offsetG"]      = kv.second.offsetG;
        t["offsetB"]      = kv.second.offsetB;
        t["offsetW"]      = kv.second.offsetW;
        t["offsetPan"]    = kv.second.offsetPan;
        t["offsetTilt"]   = kv.second.offsetTilt;
    }

    doc["master"]    = state.master;
    doc["wifi_ssid"] = state.wifi_ssid;
    doc["wifi_pass"] = state.wifi_pass;
    
    // NEU: Audio Threshold speichern
    doc["audioThreshold"] = state.audioThreshold;

    File f = LittleFS.open("/config.json", "w");
    if (f) {
        serializeJson(doc, f);
        f.close();
        Serial.println("Config gespeichert (inkl. Audio Threshold)");
    }
}

// ----------------------------------------------------
// Default Config erzeugen
// ----------------------------------------------------
void saveDefaultConfig() {
    state.fixtures.clear();
    
    // 1. Beispiel: Statische PAR Lampe
    Fixture par;
    par.name = "PAR 1";
    par.start = 1;
    par.type = "PAR7";
    par.panMin = 0; par.panMax = 255; par.tiltMin = 0; par.tiltMax = 255;
    par.isAutoMoving = false;
    state.fixtures.push_back(par);

    // 2. Beispiel: Moving Head
    Fixture mh;
    mh.name = "Head 1";
    mh.start = 10;
    mh.type = "MH14"; 
    mh.panMin = 0;   mh.panMax = 255;
    mh.tiltMin = 0;  mh.tiltMax = 255;
    mh.isAutoMoving = false;
    state.fixtures.push_back(mh);
    
    state.master = 255;
    state.wifi_ssid = "";
    state.wifi_pass = "";
    state.audioThreshold = 45; // Default Threshold

    saveConfig();
    Serial.println("Default config.json erstellt");
}

// ----------------------------------------------------
// Config laden (V7 Syntax)
// ----------------------------------------------------
void loadConfig() {
    loadDefaultFixtureTypes();

    if (!LittleFS.exists("/config.json")) {
        saveDefaultConfig();
        return;
    }

    File f = LittleFS.open("/config.json", "r");
    if (!f) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.println("Fehler beim Lesen der config.json");
        return;
    }

    // Fixtures laden
    state.fixtures.clear();
    JsonArray arr = doc["fixtures"].as<JsonArray>();
    for (JsonObject o : arr) {
        Fixture fx;
        fx.name  = o["name"]  | "Fixture";
        fx.start = o["start"] | 1;
        fx.type  = o["type"]  | "PAR7";
        fx.panMin = o["pMin"] | 0;
        fx.panMax = o["pMax"] | 255;
        fx.tiltMin = o["tMin"] | 0;
        fx.tiltMax = o["tMax"] | 255;
        fx.isAutoMoving = o["auto"] | false;
        state.fixtures.push_back(fx);
    }

    // Fixture-Typen laden
    JsonObject types = doc["fixtureTypes"].as<JsonObject>();
    for (JsonPair kv : types) {
        String name = kv.key().c_str();
        JsonObject t = kv.value().as<JsonObject>();
        FixtureDefinition def;
        def.channels     = t["channels"]     | 1;
        def.offsetDimmer = t["offsetDimmer"] | -1;
        def.offsetR      = t["offsetR"]      | -1;
        def.offsetG      = t["offsetG"]      | -1;
        def.offsetB      = t["offsetB"]      | -1;
        def.offsetW      = t["offsetW"]      | -1;
        def.offsetPan    = t["offsetPan"]    | -1;
        def.offsetTilt   = t["offsetTilt"]   | -1;
        state.fixtureTypes[name] = def;
    }

    state.master         = doc["master"]         | 255;
    state.wifi_ssid      = doc["wifi_ssid"]      | "";
    state.wifi_pass      = doc["wifi_pass"]      | "";
    state.audioThreshold = doc["audioThreshold"] | 45; // Laden oder Default

    updateDMXChannelCount();
    Serial.println("Config geladen");
}

// ----------------------------------------------------
// State initialisieren
// ----------------------------------------------------
void initState() {
    memset(state.dmx, 0, 512);
    memset(state.dmxPreview, 0, 512);
    loadConfig();
}
