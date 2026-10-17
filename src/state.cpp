#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Die Config liegt als JSON im NVS-Speicher (nicht im Dateisystem).
// So kann ein Dateisystem-Upload (Web-Oberfläche) die Einstellungen nie überschreiben.
#define NVS_NAMESPACE   "dmx"
#define NVS_CONFIG_KEY  "config"
#define LEGACY_CONFIG   "/config.json"   // alter Speicherort, wird einmalig übernommen

ControllerState state;
SemaphoreHandle_t stateMutex = nullptr;

// ----------------------------------------------------
// Default Fixture-Typen laden
// ----------------------------------------------------
void loadDefaultFixtureTypes() {
    state.fixtureTypes.clear();

    state.fixtureTypes["RGB3"]  = {3, -1, 0, 1, 2, -1, -1, -1};
    state.fixtureTypes["RGBW4"] = {4, -1, 0, 1, 2, 3, -1, -1};
    state.fixtureTypes["PAR7"]  = {7, 0, 1, 2, 3, 4, -1, -1};
    state.fixtureTypes["MH14"]  = {14, 6, 7, 8, 9, 10, 0, 1};
    state.fixtureTypes["DRGB"]  = {7, 0, 1, 2, 3, -1, -1, -1};
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
// DMX Kanalanzahl + Dimm-Maske berechnen
// ----------------------------------------------------
static void markDimmable(int base, int offset) {
    if (offset < 0) return;
    int ch = base + offset;
    if (ch >= 0 && ch < 512) state.dimmable[ch] = true;
}

void updateDMXChannelCount() {
    int maxChannel = 0;
    memset(state.dimmable, 0, sizeof(state.dimmable));

    for (auto &fx : state.fixtures) {
        const FixtureDefinition *def = getFixtureDef(fx.type);
        if (!def) continue;
        int end = fx.start + def->channels - 1;
        if (end > maxChannel) maxChannel = end;

        // Master nur einmal anwenden: auf den Dimmer-Kanal, falls vorhanden,
        // sonst auf die Farbkanäle (sonst wirkt der Master quadratisch)
        int base = fx.start - 1;
        if (def->offsetDimmer >= 0) {
            markDimmable(base, def->offsetDimmer);
        } else {
            markDimmable(base, def->offsetR);
            markDimmable(base, def->offsetG);
            markDimmable(base, def->offsetB);
            markDimmable(base, def->offsetW);
        }
    }
    state.numChannels = maxChannel;
    if (state.numChannels > 512) state.numChannels = 512;
}

// ----------------------------------------------------
// Config speichern (JSON im NVS)
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
    doc["dropEnabled"]    = state.dropEnabled;

    // Effekt-Einstellungen
    doc["effect"]     = state.currentEffect;
    doc["timerSpeed"] = state.timerSpeed;
    doc["colorR"]     = state.colorR;
    doc["colorG"]     = state.colorG;
    doc["colorB"]     = state.colorB;

    String json;
    serializeJson(doc, json);

    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("Fehler: NVS konnte nicht geöffnet werden");
        return;
    }
    size_t written = prefs.putBytes(NVS_CONFIG_KEY, json.c_str(), json.length());
    prefs.end();

    if (written == json.length()) Serial.printf("Config gespeichert (%u Bytes)\n", (unsigned)written);
    else Serial.println("Fehler: Config konnte nicht gespeichert werden");
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
    Serial.println("Default-Config erstellt");
}

// ----------------------------------------------------
// Config aus dem NVS lesen
// ----------------------------------------------------
static bool readConfigFromNVS(JsonDocument &doc) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false; // Namespace existiert noch nicht

    size_t len = prefs.getBytesLength(NVS_CONFIG_KEY);
    bool ok = false;
    if (len > 0) {
        std::vector<char> buf(len);
        prefs.getBytes(NVS_CONFIG_KEY, buf.data(), len);
        ok = !deserializeJson(doc, buf.data(), len);
        if (!ok) Serial.println("Fehler: Config im NVS ist beschädigt");
    }
    prefs.end();
    return ok;
}

// Einmalige Übernahme der alten /config.json aus LittleFS
static bool readLegacyConfig(JsonDocument &doc) {
    if (!LittleFS.exists(LEGACY_CONFIG)) return false;
    File f = LittleFS.open(LEGACY_CONFIG, "r");
    if (!f) return false;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    // Uralte Formate (z. B. "fixtures": 4) nicht übernehmen
    if (err || !doc["fixtures"].is<JsonArray>()) {
        Serial.println("Alte config.json ist ungültig → wird ignoriert");
        doc.clear();
        return false;
    }
    return true;
}

// ----------------------------------------------------
// Config laden
// ----------------------------------------------------
void loadConfig() {
    loadDefaultFixtureTypes();

    JsonDocument doc;
    bool migrated = false;
    if (!readConfigFromNVS(doc)) {
        migrated = readLegacyConfig(doc);
        if (!migrated) {
            saveDefaultConfig();
            if (LittleFS.exists(LEGACY_CONFIG)) LittleFS.remove(LEGACY_CONFIG);
            return;
        }
    }

    // Fixtures laden
    state.fixtures.clear();
    JsonArray arr = doc["fixtures"].as<JsonArray>();
    for (JsonObject o : arr) {
        Fixture fx;
        fx.name  = o["name"]  | "Fixture";
        fx.start = constrain((int)(o["start"] | 1), 1, 512);
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
    state.dropEnabled    = doc["dropEnabled"]    | true;
    state.currentEffect  = doc["effect"]         | "static";
    state.timerSpeed     = constrain((int)(doc["timerSpeed"] | 500), TIMER_SPEED_MIN, TIMER_SPEED_MAX);
    state.colorR         = doc["colorR"]         | 255;
    state.colorG         = doc["colorG"]         | 0;
    state.colorB         = doc["colorB"]         | 0;

    updateDMXChannelCount();

    if (migrated) {
        // Ab jetzt nur noch NVS; die alte Datei wird nicht mehr gebraucht
        saveConfig();
        LittleFS.remove(LEGACY_CONFIG);
        Serial.println("Config aus config.json ins NVS übernommen");
    } else {
        Serial.println("Config geladen");
    }
}

// ----------------------------------------------------
// State initialisieren
// ----------------------------------------------------
void initState() {
    if (!stateMutex) stateMutex = xSemaphoreCreateMutex();
    memset(state.dmx, 0, 512);
    memset(state.dmxPreview, 0, 512);
    loadConfig();
}
