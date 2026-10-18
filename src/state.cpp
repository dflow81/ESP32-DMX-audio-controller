#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// The config is stored as JSON in NVS (not in the file system).
// That way a file system upload (web interface) can never overwrite the settings.
#define NVS_NAMESPACE   "dmx"
#define NVS_CONFIG_KEY  "config"
#define NVS_PRESETS_KEY "presets"
#define NVS_CUES_KEY    "cues"
#define NVS_CHASES_KEY  "chases"
#define LEGACY_CONFIG   "/config.json"   // old location, migrated once

ControllerState state;
LoadStats loadStats;
SemaphoreHandle_t stateMutex = nullptr;

const char* const EXTRA_NAMES[EXTRA_COUNT]       = {"gobo", "colorWheel", "prism", "focus"};
const char* const EXTRA_OFFSET_KEYS[EXTRA_COUNT] = {"offsetGobo", "offsetColorWheel", "offsetPrism", "offsetFocus"};

// ----------------------------------------------------
// Load default fixture types
// ----------------------------------------------------
void loadDefaultFixtureTypes() {
    state.fixtureTypes.clear();

    //                            Ch  Dim  R  G  B   W  Pan Tilt  Gobo Wheel Prism Focus
    state.fixtureTypes["RGB3"]  = {3,  -1, 0, 1, 2, -1, -1, -1, {-1, -1, -1, -1}};
    state.fixtureTypes["RGBW4"] = {4,  -1, 0, 1, 2,  3, -1, -1, {-1, -1, -1, -1}};
    state.fixtureTypes["PAR7"]  = {7,   0, 1, 2, 3,  4, -1, -1, {-1, -1, -1, -1}};
    state.fixtureTypes["MH14"]  = {14,  6, 7, 8, 9, 10,  0,  1, {-1, -1, -1, -1}};
    state.fixtureTypes["DRGB"]  = {7,   0, 1, 2, 3, -1, -1, -1, {-1, -1, -1, -1}};
}

// ----------------------------------------------------
// FixtureDefinition <-> JSON (for config, /api/getConfig and /api/saveConfig)
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
// Write extra channels (gobo, color wheel, prism, focus) into the DMX buffer
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

void applyPresetData(const Preset &p, bool persist) {
    if (p.fadeMs >= 0) state.nextFadeMs = p.fadeMs;   // Transition time of this preset
    state.fxWidth = p.fxWidth;
    state.fxIntensity = p.fxIntensity;
    state.currentEffect = p.effect;
    state.colorR = p.r; state.colorG = p.g; state.colorB = p.b;
    state.timerSpeed = p.timerSpeed;
    state.master = p.master;
    if (p.hasGroups) {
        // Groups without their own effect or a disabled palette are only switched off –
        // the configured colors/effects are kept for later.
        // Group names belong to the installation, not to the preset.
        for (int g = 0; g < GROUP_COUNT; g++) {
            if (p.groups[g].custom) {
                state.groups[g].custom = true;
                state.groups[g].effect = p.groups[g].effect;
                state.groups[g].r = p.groups[g].r;
                state.groups[g].g = p.groups[g].g;
                state.groups[g].b = p.groups[g].b;
            } else {
                state.groups[g].custom = false;
            }
        }
        if (p.palette.on) state.palette = p.palette;
        else state.palette.on = false;
    }
    state.fadeRequest = true;
    if (persist) markConfigDirty();
}

void applyPreset(int index, bool persist) {
    if (index < 0 || index >= (int)state.presets.size()) return;
    state.lastPresetIndex = index;
    state.activePreset = index;
    applyPresetData(state.presets[index], persist);
}

// Tap tempo: average up to 8 taps; a pause >2 s starts a new sequence.
// From the 2nd tap the tempo is applied and locked, the beat aligned to the tap.
int registerTap() {
    static unsigned long taps[8];
    static int tapCount = 0;
    unsigned long now = millis();

    if (tapCount > 0 && now - taps[tapCount - 1] > 2000) tapCount = 0;
    if (tapCount == 8) { memmove(taps, taps + 1, 7 * sizeof(taps[0])); tapCount = 7; }
    taps[tapCount++] = now;

    if (tapCount >= 2) {
        float avg = (taps[tapCount - 1] - taps[0]) / (float)(tapCount - 1);
        state.bpm = constrain(60000.0f / avg, 40.0f, 220.0f);
        state.bpmLocked = true;
    }
    if (state.bpmLocked) state.clockSyncMs = now;
    return (int)roundf(state.bpm);
}

Preset capturePreset(const String &name) {
    Preset p;
    p.name       = name;
    p.effect     = state.currentEffect;
    p.r = state.colorR; p.g = state.colorG; p.b = state.colorB;
    p.timerSpeed = state.timerSpeed;
    p.master     = state.master;
    p.hasGroups  = true;
    for (int g = 0; g < GROUP_COUNT; g++) p.groups[g] = state.groups[g];
    p.palette    = state.palette;
    p.fadeMs     = state.fadeMs;
    p.fxWidth    = state.fxWidth;
    p.fxIntensity = state.fxIntensity;
    return p;
}

const String& effectiveEffect(int i) {
    if (i >= 0 && i < (int)state.fixtures.size()) {
        uint8_t g = state.fixtures[i].group;
        if (g >= 1 && g <= GROUP_COUNT && state.groups[g - 1].custom) return state.groups[g - 1].effect;
    }
    return state.currentEffect;
}

String colorToHex(uint8_t r, uint8_t g, uint8_t b) {
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", r, g, b);
    return String(hex);
}

void hexToColor(const String &hexIn, uint8_t &r, uint8_t &g, uint8_t &b) {
    String hex = hexIn;
    if (hex.startsWith("#")) hex = hex.substring(1);
    long n = strtol(hex.c_str(), NULL, 16);
    r = (n >> 16) & 0xFF; g = (n >> 8) & 0xFF; b = n & 0xFF;
}

// ----------------------------------------------------
// Groups / palette <-> JSON
// ----------------------------------------------------
static void groupsToJson(JsonArray arr, const GroupSettings *groups, bool withName) {
    for (int g = 0; g < GROUP_COUNT; g++) {
        JsonObject o = arr.add<JsonObject>();
        if (withName) o["name"] = groups[g].name;
        o["custom"] = groups[g].custom;
        o["effect"] = groups[g].effect;
        o["color"]  = colorToHex(groups[g].r, groups[g].g, groups[g].b);
    }
}

static void groupsFromJson(JsonArray arr, GroupSettings *groups) {
    int g = 0;
    for (JsonObject o : arr) {
        if (g >= GROUP_COUNT) break;
        if (o["name"].is<const char*>()) groups[g].name = o["name"].as<String>();
        groups[g].custom = o["custom"] | false;
        groups[g].effect = o["effect"] | "static";
        hexToColor(o["color"] | "#ff0000", groups[g].r, groups[g].g, groups[g].b);
        g++;
    }
}

static void paletteToJson(JsonObject o, const Palette &p) {
    o["on"]   = p.on;
    o["size"] = p.size;
    JsonArray c = o["colors"].to<JsonArray>();
    for (int i = 0; i < PALETTE_MAX; i++) c.add(colorToHex(p.c[i][0], p.c[i][1], p.c[i][2]));
}

static void paletteFromJson(JsonObject o, Palette &p) {
    if (o.isNull()) return;
    p.on   = o["on"] | false;
    p.size = constrain((int)(o["size"] | 3), 2, PALETTE_MAX);
    int i = 0;
    for (JsonVariant v : o["colors"].as<JsonArray>()) {
        if (i >= PALETTE_MAX) break;
        hexToColor(v | "#ffffff", p.c[i][0], p.c[i][1], p.c[i][2]);
        i++;
    }
}

// For the web API (same representation as in the config)
void groupsToJsonPublic(JsonArray arr)      { groupsToJson(arr, state.groups, true); }
void paletteToJsonPublic(JsonObject o)      { paletteToJson(o, state.palette); }

// ----------------------------------------------------
// Get FixtureDefinition
// ----------------------------------------------------
const FixtureDefinition* getFixtureDef(const String &name) {
    auto it = state.fixtureTypes.find(name);
    if (it == state.fixtureTypes.end()) return nullptr;
    return &it->second;
}

// ----------------------------------------------------
// Start address of a fixture (1-based)
// ----------------------------------------------------
int getFixtureStartAddress(int fixtureIndex) {
    if (fixtureIndex < 0 || fixtureIndex >= (int)state.fixtures.size())
        return 1;
    return state.fixtures[fixtureIndex].start;
}

// ----------------------------------------------------
// Compute DMX channel count + dimmer mask
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

        // Apply the master only once: to the dimmer channel if present,
        // otherwise to the color channels (else the master would act quadratically)
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

    applyFixtureExtras(); // Re-apply gobo & co. after every fixture change
}

// ----------------------------------------------------
// Save config (JSON in NVS)
// ----------------------------------------------------
static void buildConfigDoc(JsonDocument &doc, bool includeWifi);
static bool writeConfigToNVS(JsonDocument &doc);

void saveConfig() {
    JsonDocument doc;
    buildConfigDoc(doc, true);
    writeConfigToNVS(doc);
}

// The NVS area is only 20 KB, and when overwriting, the old and new
// data briefly have to fit at the same time. That's why the config is split into
// three entries: base config, presets and cue list.
static bool putJson(Preferences &prefs, const char *key, JsonVariantConst v) {
    String json;
    serializeJson(v, json);
    size_t written = prefs.putBytes(key, json.c_str(), json.length());
    if (written != json.length()) {
        Serial.printf("Fehler: '%s' konnte nicht gespeichert werden (%u Bytes)\n", key, (unsigned)json.length());
        return false;
    }
    return true;
}

static bool writeConfigToNVS(JsonDocument &doc) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("Fehler: NVS konnte nicht geöffnet werden");
        return false;
    }
    JsonDocument presets, cues, chases;
    presets.set(doc["presets"]);
    cues.set(doc["cues"]);
    chases.set(doc["chases"]);
    doc.remove("presets");
    doc.remove("cues");
    doc.remove("chases");

    bool ok = putJson(prefs, NVS_PRESETS_KEY, presets.as<JsonVariantConst>());
    ok = putJson(prefs, NVS_CUES_KEY, cues.as<JsonVariantConst>()) && ok;
    ok = putJson(prefs, NVS_CHASES_KEY, chases.as<JsonVariantConst>()) && ok;
    ok = putJson(prefs, NVS_CONFIG_KEY, doc.as<JsonVariantConst>()) && ok;
    size_t freeEntries = prefs.freeEntries();
    prefs.end();
    if (ok) Serial.printf("Config gespeichert (NVS frei: %u Einträge)\n", (unsigned)freeEntries);
    return ok;
}

// ----------------------------------------------------
// Backup / restore
// ----------------------------------------------------
String exportConfigJson() {
    JsonDocument doc;
    buildConfigDoc(doc, false); // The Wi-Fi password does not belong in a file lying around
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

    // Keep the device's Wi-Fi credentials (not contained in the backup)
    doc["wifi_ssid"] = state.wifi_ssid;
    doc["wifi_pass"] = state.wifi_pass;
    doc["mqtt"]["pass"] = state.mqttPass;
    doc["auth"]["pass"] = state.authPass;   // Keep the device's password

    if (!writeConfigToNVS(doc)) {
        error = "Speichern fehlgeschlagen";
        return false;
    }
    state.otaActive = true; // Block auto-save, otherwise the old state would overwrite the backup
    Serial.println("Backup wiederhergestellt, Neustart folgt");
    return true;
}

static void buildConfigDoc(JsonDocument &doc, bool includeWifi) {
    // Save fixtures incl. MH limits
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
        o["group"] = fx.group;
        JsonArray ex = o["extra"].to<JsonArray>();
        for (int e = 0; e < EXTRA_COUNT; e++) ex.add(fx.extra[e]);
    }

    // Save fixture types
    JsonObject types = doc["fixtureTypes"].to<JsonObject>();
    for (auto &kv : state.fixtureTypes) {
        fixtureDefToJson(types[kv.first].to<JsonObject>(), kv.second);
    }

    doc["master"]    = state.master;
    if (includeWifi) {
        doc["wifi_ssid"] = state.wifi_ssid;
        doc["wifi_pass"] = state.wifi_pass;
    }
    
    // Save audio threshold
    doc["audioThreshold"] = state.audioThreshold;
    doc["dropEnabled"]    = state.dropEnabled;
    doc["dropAction"]     = state.dropAction;
    doc["dropPreset"]     = state.dropPreset;

    // Beat, moving heads, auto show
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
        if (p.fadeMs >= 0) o["fade"] = p.fadeMs;
        o["w"] = p.fxWidth;
        o["i"] = p.fxIntensity;
        if (p.energy) o["e"] = p.energy;
        if (p.hasGroups) {
            // Compact: only store groups/palette when they are used (NVS is small)
            o["v2"] = true;
            bool anyCustom = false;
            for (int g = 0; g < GROUP_COUNT; g++) anyCustom |= p.groups[g].custom;
            if (anyCustom) groupsToJson(o["groups"].to<JsonArray>(), p.groups, false);
            if (p.palette.on) paletteToJson(o["palette"].to<JsonObject>(), p.palette);
        }
    }

    // Groups, palette
    groupsToJson(doc["groups"].to<JsonArray>(), state.groups, true);
    paletteToJson(doc["palette"].to<JsonObject>(), state.palette);

    // Fog
    JsonObject fog = doc["fog"].to<JsonObject>();
    fog["channel"]     = state.fogChannel;
    fog["level"]       = state.fogLevel;
    fog["onDrop"]      = state.fogOnDrop;
    fog["dropSec"]     = state.fogDropSec;
    fog["intervalMin"] = state.fogIntervalMin;
    fog["intervalSec"] = state.fogIntervalSec;

    // Transitions, effect parameters, volume-driven brightness
    doc["fadeMs"]        = state.fadeMs;
    doc["fxWidth"]       = state.fxWidth;
    doc["fxIntensity"]   = state.fxIntensity;
    doc["levelDim"]      = state.levelDim;
    doc["levelDimDepth"] = state.levelDimDepth;

    // Moving head positions
    JsonArray positions = doc["positions"].to<JsonArray>();
    for (auto &pos : state.positions) {
        JsonObject o = positions.add<JsonObject>();
        o["name"] = pos.name;
        JsonArray heads = o["heads"].to<JsonArray>();
        for (auto &h : pos.heads) {
            JsonObject ho = heads.add<JsonObject>();
            ho["f"] = h.fixture; ho["p"] = h.pan; ho["t"] = h.tilt;
        }
    }

    doc["lang"] = state.langEn ? "en" : "de";

    // Password protection (password not in the backup)
    JsonObject auth = doc["auth"].to<JsonObject>();
    auth["on"]   = state.authOn;
    auth["user"] = state.authUser;
    if (includeWifi) auth["pass"] = state.authPass;

    // Smart auto show, sync, MQTT
    doc["autoShowSmart"] = state.autoShowSmart;
    JsonObject sync = doc["sync"].to<JsonObject>();
    sync["role"]  = state.syncRole;
    sync["group"] = state.syncGroup;
    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["on"]    = state.mqttOn;
    mqtt["host"]  = state.mqttHost;
    mqtt["port"]  = state.mqttPort;
    mqtt["user"]  = state.mqttUser;
    if (includeWifi) mqtt["pass"] = state.mqttPass;   // Password not in the backup
    mqtt["topic"] = state.mqttTopic;

    // Custom chases
    JsonArray chases = doc["chases"].to<JsonArray>();
    for (int c = 0; c < USER_CHASES; c++) {
        const UserChase &uc = state.userChases[c];
        JsonObject o = chases.add<JsonObject>();
        if (uc.name.isEmpty()) continue;
        o["name"]   = uc.name;
        o["w"]      = uc.width;
        o["s"]      = uc.steps;
        o["smooth"] = uc.smooth;
        // Compact: one string per step "rrggbbrrggbb…"
        JsonArray rows = o["rows"].to<JsonArray>();
        for (int st = 0; st < uc.steps; st++) {
            String row;
            for (int x = 0; x < uc.width; x++) {
                char hex[7];
                snprintf(hex, sizeof(hex), "%06lx", (unsigned long)(uc.cells[st][x] & 0xFFFFFF));
                row += hex;
            }
            rows.add(row);
        }
    }

    // Schedule, display
    JsonObject sched = doc["schedule"].to<JsonObject>();
    sched["on"]     = state.scheduleOn;
    sched["start"]  = state.scheduleStart;
    sched["end"]    = state.scheduleEnd;
    sched["preset"] = state.schedulePreset;
    doc["displayOffMin"] = state.displayOffMin;

    // Standby
    doc["standbyMin"]    = state.standbyMin;
    doc["standbyPreset"] = state.standbyPreset;

    // Cue list
    JsonArray cues = doc["cues"].to<JsonArray>();
    for (auto &c : state.cues) {
        JsonObject o = cues.add<JsonObject>();
        o["preset"] = c.preset;
        o["dur"]    = c.duration;
        o["beats"]  = c.beats;
    }
    doc["cueLoop"] = state.cueLoop;

    // Microphone calibration
    JsonObject cal = doc["calibration"].to<JsonObject>();
    cal["done"]      = state.calibrated;
    cal["gateKick"]  = state.gateKick;
    cal["gateSnare"] = state.gateSnare;
    cal["gateHihat"] = state.gateHihat;
    cal["refKick"]   = state.refKick;
    cal["refSnare"]  = state.refSnare;
    cal["refHihat"]  = state.refHihat;
    cal["refLevel"]  = state.refLevel;

    // Effect settings
    doc["effect"]     = state.currentEffect;
    doc["timerSpeed"] = state.timerSpeed;
    doc["colorR"]     = state.colorR;
    doc["colorG"]     = state.colorG;
    doc["colorB"]     = state.colorB;
}

// ----------------------------------------------------
// Create default config
// ----------------------------------------------------
void saveDefaultConfig() {
    state.fixtures.clear();
    
    // 1st example: static PAR lamp
    Fixture par;
    par.name = "PAR 1";
    par.start = 1;
    par.type = "PAR7";
    par.panMin = 0; par.panMax = 255; par.tiltMin = 0; par.tiltMax = 255;
    par.isAutoMoving = false;
    state.fixtures.push_back(par);

    // 2nd example: moving head
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
    state.audioThreshold = 45; // Default threshold

    saveConfig();
    Serial.println("Default-Config erstellt");
}

// ----------------------------------------------------
// Read config from NVS
// ----------------------------------------------------
static bool readConfigFromNVS(JsonDocument &doc) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false; // Namespace does not exist yet

    size_t len = prefs.getBytesLength(NVS_CONFIG_KEY);
    bool ok = false;
    if (len > 0) {
        std::vector<char> buf(len);
        prefs.getBytes(NVS_CONFIG_KEY, buf.data(), len);
        ok = !deserializeJson(doc, buf.data(), len);
        if (!ok) Serial.println("Fehler: Config im NVS ist beschädigt");
    }

    // Presets and cue list are stored in their own entries (older versions: in "config")
    const char *keys[3] = {NVS_PRESETS_KEY, NVS_CUES_KEY, NVS_CHASES_KEY};
    for (const char *key : keys) {
        if (!ok || doc[key].is<JsonArray>()) continue;
        size_t n = prefs.getBytesLength(key);
        if (n == 0) continue;
        std::vector<char> buf(n);
        prefs.getBytes(key, buf.data(), n);
        JsonDocument part;
        if (!deserializeJson(part, buf.data(), n)) doc[key] = part;
        else Serial.printf("Fehler: '%s' im NVS ist beschädigt\n", key);
    }
    prefs.end();
    return ok;
}

// One-time migration of the old /config.json from LittleFS
static bool readLegacyConfig(JsonDocument &doc) {
    if (!LittleFS.exists(LEGACY_CONFIG)) return false;
    File f = LittleFS.open(LEGACY_CONFIG, "r");
    if (!f) return false;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    // Don't migrate ancient formats (e.g. "fixtures": 4)
    if (err || !doc["fixtures"].is<JsonArray>()) {
        Serial.println("Alte config.json ist ungültig → wird ignoriert");
        doc.clear();
        return false;
    }
    return true;
}

// ----------------------------------------------------
// Load config
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

    // Load fixtures
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
        fx.group = constrain((int)(o["group"] | 0), 0, GROUP_COUNT);
        JsonArray ex = o["extra"].as<JsonArray>();
        for (int e = 0; e < EXTRA_COUNT && e < (int)ex.size(); e++) fx.extra[e] = ex[e] | 0;
        state.fixtures.push_back(fx);
    }

    // Load fixture types
    JsonObject types = doc["fixtureTypes"].as<JsonObject>();
    for (JsonPair kv : types) {
        String name = kv.key().c_str();
        state.fixtureTypes[name] = fixtureDefFromJson(kv.value().as<JsonObject>());
    }

    state.master         = doc["master"]         | 255;
    state.wifi_ssid      = doc["wifi_ssid"]      | "";
    state.wifi_pass      = doc["wifi_pass"]      | "";
    state.audioThreshold = doc["audioThreshold"] | 45; // Load or default
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

    // Load presets
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
        p.fadeMs     = o["fade"].is<int>() ? constrain((int)o["fade"], 0, 10000) : -1;
        p.fxWidth    = constrain((int)(o["w"] | 1), 1, 8);
        p.fxIntensity = constrain((int)(o["i"] | 50), 0, 100);
        p.energy     = constrain((int)(o["e"] | 0), 0, 3);
        if ((o["v2"] | false) || o["groups"].is<JsonArray>()) {
            p.hasGroups = true;   // missing groups/palette = not used (custom/on = false)
            groupsFromJson(o["groups"].as<JsonArray>(), p.groups);
            paletteFromJson(o["palette"].as<JsonObject>(), p.palette);
        }
        state.presets.push_back(p);
    }

    // Groups, palette
    groupsFromJson(doc["groups"].as<JsonArray>(), state.groups);
    paletteFromJson(doc["palette"].as<JsonObject>(), state.palette);

    // Fog
    JsonObject fog = doc["fog"].as<JsonObject>();
    state.fogChannel     = constrain((int)(fog["channel"]     | 0), 0, 512);
    state.fogLevel       = fog["level"]       | 255;
    state.fogOnDrop      = fog["onDrop"]      | false;
    state.fogDropSec     = constrain((int)(fog["dropSec"]     | 3), 1, 30);
    state.fogIntervalMin = constrain((int)(fog["intervalMin"] | 0), 0, 120);
    state.fogIntervalSec = constrain((int)(fog["intervalSec"] | 5), 1, 30);

    // Transitions, effect parameters, volume-driven brightness
    state.fadeMs        = constrain((int)(doc["fadeMs"] | 600), 0, 10000);
    state.fxWidth       = constrain((int)(doc["fxWidth"] | 1), 1, 8);
    state.fxIntensity   = constrain((int)(doc["fxIntensity"] | 50), 0, 100);
    state.levelDim      = doc["levelDim"] | false;
    state.levelDimDepth = constrain((int)(doc["levelDimDepth"] | 50), 0, 100);

    // Moving head positions
    state.positions.clear();
    for (JsonObject o : doc["positions"].as<JsonArray>()) {
        if (state.positions.size() >= MAX_POSITIONS) break;
        MhPosition pos;
        pos.name = o["name"] | "Position";
        for (JsonObject h : o["heads"].as<JsonArray>()) {
            pos.heads.push_back({h["f"] | "", (uint8_t)(h["p"] | 128), (uint8_t)(h["t"] | 128)});
        }
        state.positions.push_back(pos);
    }

    state.langEn = String(doc["lang"] | "de") == "en";

    // Password protection
    JsonObject auth = doc["auth"].as<JsonObject>();
    state.authOn   = auth["on"]   | false;
    state.authUser = auth["user"] | "admin";
    if (auth["pass"].is<const char*>()) state.authPass = auth["pass"].as<String>();

    // Smart auto show, sync, MQTT
    state.autoShowSmart = doc["autoShowSmart"] | false;
    JsonObject sync = doc["sync"].as<JsonObject>();
    state.syncRole  = constrain((int)(sync["role"] | 0), 0, 2);
    state.syncGroup = constrain((int)(sync["group"] | 1), 1, 9);
    JsonObject mqtt = doc["mqtt"].as<JsonObject>();
    state.mqttOn    = mqtt["on"]    | false;
    state.mqttHost  = mqtt["host"]  | "";
    state.mqttPort  = constrain((int)(mqtt["port"] | 1883), 1, 65535);
    state.mqttUser  = mqtt["user"]  | "";
    if (mqtt["pass"].is<const char*>()) state.mqttPass = mqtt["pass"].as<String>();
    state.mqttTopic = mqtt["topic"] | "dmxcontroller";

    // Custom chases
    int ci = 0;
    for (JsonObject o : doc["chases"].as<JsonArray>()) {
        if (ci >= USER_CHASES) break;
        UserChase &uc = state.userChases[ci++];
        uc = UserChase();
        uc.name   = o["name"] | "";
        uc.width  = constrain((int)(o["w"] | 4), 1, USER_CHASE_WIDTH);
        uc.steps  = constrain((int)(o["s"] | 4), 1, USER_CHASE_STEPS);
        uc.smooth = o["smooth"] | false;
        int st = 0;
        for (JsonVariant rv : o["rows"].as<JsonArray>()) {
            if (st >= USER_CHASE_STEPS) break;
            String row = rv.as<String>();
            for (int x = 0; x < uc.width && (x + 1) * 6 <= (int)row.length(); x++) {
                uc.cells[st][x] = strtoul(row.substring(x * 6, x * 6 + 6).c_str(), NULL, 16);
            }
            st++;
        }
    }

    // Schedule, display
    JsonObject sched = doc["schedule"].as<JsonObject>();
    state.scheduleOn     = sched["on"]     | false;
    state.scheduleStart  = sched["start"]  | "18:00";
    state.scheduleEnd    = sched["end"]    | "02:00";
    state.schedulePreset = sched["preset"] | "";
    state.displayOffMin  = constrain((int)(doc["displayOffMin"] | 15), 0, 240);

    // Standby
    state.standbyMin    = constrain((int)(doc["standbyMin"] | 0), 0, 240);
    state.standbyPreset = doc["standbyPreset"] | "";

    // Cue list
    state.cues.clear();
    for (JsonObject o : doc["cues"].as<JsonArray>()) {
        if (state.cues.size() >= MAX_CUES) break;
        Cue c;
        c.preset   = o["preset"] | "";
        c.duration = constrain((int)(o["dur"] | 16), 1, 9999);
        c.beats    = o["beats"]  | true;
        state.cues.push_back(c);
    }
    state.cueLoop = doc["cueLoop"] | true;

    // Microphone calibration
    JsonObject cal = doc["calibration"].as<JsonObject>();
    state.calibrated = cal["done"]      | false;
    state.gateKick   = cal["gateKick"]  | 0.0;
    state.gateSnare  = cal["gateSnare"] | 0.0;
    state.gateHihat  = cal["gateHihat"] | 0.0;
    state.refKick    = cal["refKick"]   | 180000.0;
    state.refSnare   = cal["refSnare"]  | 190000.0;
    state.refHihat   = cal["refHihat"]  | 170000.0;
    state.refLevel   = cal["refLevel"]  | 5000.0;

    // Effect "retro" is now called "lightorgan" – rename stored names
    auto rename = [](String &e) { if (e == "retro") e = "lightorgan"; };
    rename(state.currentEffect);
    for (auto &p : state.presets) {
        rename(p.effect);
        for (auto &g : p.groups) rename(g.effect);
    }
    for (auto &g : state.groups) rename(g.effect);

    updateDMXChannelCount();

    if (migrated) {
        // From now on only NVS; the old file is no longer needed
        saveConfig();
        LittleFS.remove(LEGACY_CONFIG);
        Serial.println("Config aus config.json ins NVS übernommen");
    } else {
        Serial.println("Config geladen");
    }
}

// ----------------------------------------------------
// Initialize state
// ----------------------------------------------------
void initState() {
    if (!stateMutex) stateMutex = xSemaphoreCreateMutex();
    memset(state.dmx, 0, 512);
    memset(state.dmxPreview, 0, 512);
    loadConfig();
}
