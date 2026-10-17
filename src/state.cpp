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

const char* const EXTRA_NAMES[EXTRA_COUNT]       = {"gobo", "colorWheel", "prism", "focus"};
const char* const EXTRA_OFFSET_KEYS[EXTRA_COUNT] = {"offsetGobo", "offsetColorWheel", "offsetPrism", "offsetFocus"};

// ----------------------------------------------------
// Default Fixture-Typen laden
// ----------------------------------------------------
void loadDefaultFixtureTypes() {
    state.fixtureTypes.clear();

    //                            Ch  Dim  R  G  B   W  Pan Tilt  Gobo Farbrad Prisma Fokus
    state.fixtureTypes["RGB3"]  = {3,  -1, 0, 1, 2, -1, -1, -1, {-1, -1, -1, -1}};
    state.fixtureTypes["RGBW4"] = {4,  -1, 0, 1, 2,  3, -1, -1, {-1, -1, -1, -1}};
    state.fixtureTypes["PAR7"]  = {7,   0, 1, 2, 3,  4, -1, -1, {-1, -1, -1, -1}};
    state.fixtureTypes["MH14"]  = {14,  6, 7, 8, 9, 10,  0,  1, {-1, -1, -1, -1}};
    state.fixtureTypes["DRGB"]  = {7,   0, 1, 2, 3, -1, -1, -1, {-1, -1, -1, -1}};
}

// ----------------------------------------------------
// FixtureDefinition <-> JSON (für Config, /api/getConfig und /api/saveConfig)
// ----------------------------------------------------
void fixtureDefToJson(JsonObject t, const FixtureDefinition &d) {
    t["channels"]     = d.channels;
    t["offsetDimmer"] = d.offsetDimmer;
    t["offsetR"]      = d.offsetR;
    t["offsetG"]      = d.offsetG;
    t["offsetB"]      = d.offsetB;
    t["offsetW"]      = d.offsetW;
    t["offsetPan"]    = d.offsetPan;
    t["offsetTilt"]   = d.offsetTilt;
    for (int e = 0; e < EXTRA_COUNT; e++) t[EXTRA_OFFSET_KEYS[e]] = d.offsetExtra[e];
}

FixtureDefinition fixtureDefFromJson(JsonObject t) {
    FixtureDefinition d;
    d.channels     = constrain((int)(t["channels"] | 1), 1, 512);
    d.offsetDimmer = t["offsetDimmer"] | -1;
    d.offsetR      = t["offsetR"]      | -1;
    d.offsetG      = t["offsetG"]      | -1;
    d.offsetB      = t["offsetB"]      | -1;
    d.offsetW      = t["offsetW"]      | -1;
    d.offsetPan    = t["offsetPan"]    | -1;
    d.offsetTilt   = t["offsetTilt"]   | -1;
    for (int e = 0; e < EXTRA_COUNT; e++) d.offsetExtra[e] = t[EXTRA_OFFSET_KEYS[e]] | -1;
    return d;
}

// ----------------------------------------------------
// Zusatzkanäle (Gobo, Farbrad, Prisma, Fokus) in den DMX-Puffer schreiben
// ----------------------------------------------------
void applyFixtureExtras() {
    for (auto &fx : state.fixtures) {
        const FixtureDefinition *def = getFixtureDef(fx.type);
        if (!def) continue;
        for (int e = 0; e < EXTRA_COUNT; e++) {
            if (def->offsetExtra[e] < 0) continue;
            int ch = fx.start - 1 + def->offsetExtra[e];
            if (ch >= 0 && ch < 512) state.dmx[ch] = fx.extra[e];
        }
    }
}

// ----------------------------------------------------
// Presets
// ----------------------------------------------------
int findPreset(const String &name) {
    for (size_t i = 0; i < state.presets.size(); i++) {
        if (state.presets[i].name == name) return i;
    }
    return -1;
}

void applyPreset(int index, bool persist) {
    if (index < 0 || index >= (int)state.presets.size()) return;
    const Preset &p = state.presets[index];
    state.currentEffect = p.effect;
    state.colorR = p.r; state.colorG = p.g; state.colorB = p.b;
    state.timerSpeed = p.timerSpeed;
    state.master = p.master;
    state.fadeRequest = true;
    if (persist) markConfigDirty();
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

    applyFixtureExtras(); // Gobo & Co. nach jeder Änderung der Fixtures neu setzen
}

// ----------------------------------------------------
// Config speichern (JSON im NVS)
// ----------------------------------------------------
static void buildConfigDoc(JsonDocument &doc, bool includeWifi);
static bool writeConfigToNVS(const String &json);

void saveConfig() {
    JsonDocument doc;
    buildConfigDoc(doc, true);

    String json;
    serializeJson(doc, json);
    if (writeConfigToNVS(json)) Serial.printf("Config gespeichert (%u Bytes)\n", (unsigned)json.length());
}

static bool writeConfigToNVS(const String &json) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("Fehler: NVS konnte nicht geöffnet werden");
        return false;
    }
    size_t written = prefs.putBytes(NVS_CONFIG_KEY, json.c_str(), json.length());
    prefs.end();
    if (written != json.length()) {
        Serial.println("Fehler: Config konnte nicht gespeichert werden");
        return false;
    }
    return true;
}

// ----------------------------------------------------
// Backup / Wiederherstellung
// ----------------------------------------------------
String exportConfigJson() {
    JsonDocument doc;
    buildConfigDoc(doc, false); // WLAN-Passwort gehört nicht in eine Datei, die herumliegt
    String json;
    serializeJsonPretty(doc, json);
    return json;
}

bool importConfigJson(const char *json, size_t len, String &error) {
    JsonDocument doc;
    if (deserializeJson(doc, json, len)) {
        error = "Datei ist kein gültiges JSON";
        return false;
    }
    if (!doc["fixtures"].is<JsonArray>() || !doc["fixtureTypes"].is<JsonObject>()) {
        error = "Datei ist kein Backup dieses Controllers";
        return false;
    }

    // WLAN-Daten des Geräts behalten (sind im Backup nicht enthalten)
    doc["wifi_ssid"] = state.wifi_ssid;
    doc["wifi_pass"] = state.wifi_pass;

    String out;
    serializeJson(doc, out);
    if (!writeConfigToNVS(out)) {
        error = "Speichern fehlgeschlagen";
        return false;
    }
    state.otaActive = true; // Auto-Save sperren, sonst überschreibt der alte Stand das Backup
    Serial.println("Backup wiederhergestellt, Neustart folgt");
    return true;
}

static void buildConfigDoc(JsonDocument &doc, bool includeWifi) {
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
        JsonArray ex = o["extra"].to<JsonArray>();
        for (int e = 0; e < EXTRA_COUNT; e++) ex.add(fx.extra[e]);
    }

    // Fixture-Typen speichern
    JsonObject types = doc["fixtureTypes"].to<JsonObject>();
    for (auto &kv : state.fixtureTypes) {
        fixtureDefToJson(types[kv.first].to<JsonObject>(), kv.second);
    }

    doc["master"]    = state.master;
    if (includeWifi) {
        doc["wifi_ssid"] = state.wifi_ssid;
        doc["wifi_pass"] = state.wifi_pass;
    }
    
    // NEU: Audio Threshold speichern
    doc["audioThreshold"] = state.audioThreshold;
    doc["dropEnabled"]    = state.dropEnabled;
    doc["dropAction"]     = state.dropAction;
    doc["dropPreset"]     = state.dropPreset;

    // Takt, Moving Heads, Auto-Show
    doc["beatSync"]     = state.beatSync;
    doc["stepsPerBeat"] = state.stepsPerBeat;
    doc["movePattern"]  = state.movePattern;
    doc["moveSpread"]   = state.moveSpread;
    doc["autoShow"]     = state.autoShow;
    doc["showBeats"]    = state.showBeats;
    doc["netInput"]     = state.netInput;
    doc["netUniverse"]  = state.netUniverse;

    // Presets
    JsonArray presets = doc["presets"].to<JsonArray>();
    for (auto &p : state.presets) {
        JsonObject o = presets.add<JsonObject>();
        o["name"]   = p.name;
        o["effect"] = p.effect;
        o["r"] = p.r; o["g"] = p.g; o["b"] = p.b;
        o["speed"]  = p.timerSpeed;
        o["master"] = p.master;
    }

    // Effekt-Einstellungen
    doc["effect"]     = state.currentEffect;
    doc["timerSpeed"] = state.timerSpeed;
    doc["colorR"]     = state.colorR;
    doc["colorG"]     = state.colorG;
    doc["colorB"]     = state.colorB;
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
        JsonArray ex = o["extra"].as<JsonArray>();
        for (int e = 0; e < EXTRA_COUNT && e < (int)ex.size(); e++) fx.extra[e] = ex[e] | 0;
        state.fixtures.push_back(fx);
    }

    // Fixture-Typen laden
    JsonObject types = doc["fixtureTypes"].as<JsonObject>();
    for (JsonPair kv : types) {
        String name = kv.key().c_str();
        state.fixtureTypes[name] = fixtureDefFromJson(kv.value().as<JsonObject>());
    }

    state.master         = doc["master"]         | 255;
    state.wifi_ssid      = doc["wifi_ssid"]      | "";
    state.wifi_pass      = doc["wifi_pass"]      | "";
    state.audioThreshold = doc["audioThreshold"] | 45; // Laden oder Default
    state.dropEnabled    = doc["dropEnabled"]    | true;
    state.dropAction     = constrain((int)(doc["dropAction"] | (int)DROP_STROBE), 0, DROP_ACTION_COUNT - 1);
    state.dropPreset     = doc["dropPreset"]     | "";
    state.beatSync       = doc["beatSync"]       | false;
    state.stepsPerBeat   = doc["stepsPerBeat"]   | 1.0f;
    if (state.stepsPerBeat != 2.0f && state.stepsPerBeat != 0.5f) state.stepsPerBeat = 1.0f;
    state.movePattern    = constrain((int)(doc["movePattern"] | (int)MOVE_CIRCLE), 0, MOVE_PATTERN_COUNT - 1);
    state.moveSpread     = doc["moveSpread"]     | true;
    state.autoShow       = doc["autoShow"]       | false;
    state.showBeats      = constrain((int)(doc["showBeats"] | 32), 4, 256);
    state.netInput       = doc["netInput"]       | false;
    state.netUniverse    = constrain((int)(doc["netUniverse"] | 1), 1, 32768);
    state.currentEffect  = doc["effect"]         | "static";
    state.timerSpeed     = constrain((int)(doc["timerSpeed"] | 500), TIMER_SPEED_MIN, TIMER_SPEED_MAX);
    state.colorR         = doc["colorR"]         | 255;
    state.colorG         = doc["colorG"]         | 0;
    state.colorB         = doc["colorB"]         | 0;

    // Presets laden
    state.presets.clear();
    for (JsonObject o : doc["presets"].as<JsonArray>()) {
        if (state.presets.size() >= MAX_PRESETS) break;
        Preset p;
        p.name       = o["name"]   | "Preset";
        p.effect     = o["effect"] | "static";
        p.r          = o["r"]      | 255;
        p.g          = o["g"]      | 0;
        p.b          = o["b"]      | 0;
        p.timerSpeed = constrain((int)(o["speed"] | 500), TIMER_SPEED_MIN, TIMER_SPEED_MAX);
        p.master     = constrain((int)(o["master"] | 255), 0, 255);
        state.presets.push_back(p);
    }

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
