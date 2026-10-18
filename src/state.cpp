#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// Die Config liegt als JSON im NVS-Speicher (nicht im Dateisystem).
// So kann ein Dateisystem-Upload (Web-Oberfläche) die Einstellungen nie überschreiben.
#define NVS_NAMESPACE   "dmx"
#define NVS_CONFIG_KEY  "config"
#define NVS_PRESETS_KEY "presets"
#define NVS_CUES_KEY    "cues"
#define LEGACY_CONFIG   "/config.json"   // alter Speicherort, wird einmalig übernommen

ControllerState state;
LoadStats loadStats;
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

void applyPresetData(const Preset &p, bool persist) {
    if (p.fadeMs >= 0) state.nextFadeMs = p.fadeMs;   // Übergangszeit dieses Presets
    state.fxWidth = p.fxWidth;
    state.fxIntensity = p.fxIntensity;
    state.currentEffect = p.effect;
    state.colorR = p.r; state.colorG = p.g; state.colorB = p.b;
    state.timerSpeed = p.timerSpeed;
    state.master = p.master;
    if (p.hasGroups) {
        // Gruppen ohne eigenen Effekt bzw. eine ausgeschaltete Palette nur abschalten –
        // die eingestellten Farben/Effekte bleiben für später erhalten.
        // Gruppennamen gehören zur Anlage, nicht zum Preset.
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

// Tap-Tempo: bis zu 8 Taps mitteln; >2 s Pause beginnt eine neue Folge.
// Ab dem 2. Tap wird das Tempo übernommen und festgehalten, der Takt auf den Tap ausgerichtet.
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
// Gruppen / Palette <-> JSON
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

// Für die Web-API (gleiche Darstellung wie in der Config)
void groupsToJsonPublic(JsonArray arr)      { groupsToJson(arr, state.groups, true); }
void paletteToJsonPublic(JsonObject o)      { paletteToJson(o, state.palette); }

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
static bool writeConfigToNVS(JsonDocument &doc);

void saveConfig() {
    JsonDocument doc;
    buildConfigDoc(doc, true);
    writeConfigToNVS(doc);
}

// Der NVS-Bereich ist nur 20 KB groß, und beim Überschreiben müssen alter und neuer
// Stand kurz gleichzeitig hineinpassen. Deshalb wird die Config in drei Einträge
// aufgeteilt: Grund-Config, Presets und Cue-Liste.
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
    JsonDocument presets, cues;
    presets.set(doc["presets"]);
    cues.set(doc["cues"]);
    doc.remove("presets");
    doc.remove("cues");

    bool ok = putJson(prefs, NVS_PRESETS_KEY, presets.as<JsonVariantConst>());
    ok = putJson(prefs, NVS_CUES_KEY, cues.as<JsonVariantConst>()) && ok;
    ok = putJson(prefs, NVS_CONFIG_KEY, doc.as<JsonVariantConst>()) && ok;
    size_t freeEntries = prefs.freeEntries();
    prefs.end();
    if (ok) Serial.printf("Config gespeichert (NVS frei: %u Einträge)\n", (unsigned)freeEntries);
    return ok;
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

    if (!writeConfigToNVS(doc)) {
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
        o["group"] = fx.group;
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
        if (p.fadeMs >= 0) o["fade"] = p.fadeMs;
        o["w"] = p.fxWidth;
        o["i"] = p.fxIntensity;
        if (p.hasGroups) {
            // Kompakt: Gruppen/Palette nur speichern, wenn sie benutzt werden (NVS ist klein)
            o["v2"] = true;
            bool anyCustom = false;
            for (int g = 0; g < GROUP_COUNT; g++) anyCustom |= p.groups[g].custom;
            if (anyCustom) groupsToJson(o["groups"].to<JsonArray>(), p.groups, false);
            if (p.palette.on) paletteToJson(o["palette"].to<JsonObject>(), p.palette);
        }
    }

    // Gruppen, Palette
    groupsToJson(doc["groups"].to<JsonArray>(), state.groups, true);
    paletteToJson(doc["palette"].to<JsonObject>(), state.palette);

    // Nebel
    JsonObject fog = doc["fog"].to<JsonObject>();
    fog["channel"]     = state.fogChannel;
    fog["level"]       = state.fogLevel;
    fog["onDrop"]      = state.fogOnDrop;
    fog["dropSec"]     = state.fogDropSec;
    fog["intervalMin"] = state.fogIntervalMin;
    fog["intervalSec"] = state.fogIntervalSec;

    // Übergänge, Effekt-Parameter, Lautstärke-Helligkeit
    doc["fadeMs"]        = state.fadeMs;
    doc["fxWidth"]       = state.fxWidth;
    doc["fxIntensity"]   = state.fxIntensity;
    doc["levelDim"]      = state.levelDim;
    doc["levelDimDepth"] = state.levelDimDepth;

    // Moving-Head-Positionen
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

    // Zeitplan, Display
    JsonObject sched = doc["schedule"].to<JsonObject>();
    sched["on"]     = state.scheduleOn;
    sched["start"]  = state.scheduleStart;
    sched["end"]    = state.scheduleEnd;
    sched["preset"] = state.schedulePreset;
    doc["displayOffMin"] = state.displayOffMin;

    // Standby
    doc["standbyMin"]    = state.standbyMin;
    doc["standbyPreset"] = state.standbyPreset;

    // Cue-Liste
    JsonArray cues = doc["cues"].to<JsonArray>();
    for (auto &c : state.cues) {
        JsonObject o = cues.add<JsonObject>();
        o["preset"] = c.preset;
        o["dur"]    = c.duration;
        o["beats"]  = c.beats;
    }
    doc["cueLoop"] = state.cueLoop;

    // Mikrofon-Kalibrierung
    JsonObject cal = doc["calibration"].to<JsonObject>();
    cal["done"]      = state.calibrated;
    cal["gateKick"]  = state.gateKick;
    cal["gateSnare"] = state.gateSnare;
    cal["gateHihat"] = state.gateHihat;
    cal["refKick"]   = state.refKick;
    cal["refSnare"]  = state.refSnare;
    cal["refHihat"]  = state.refHihat;
    cal["refLevel"]  = state.refLevel;

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

    // Presets und Cue-Liste liegen in eigenen Einträgen (ältere Stände: in "config")
    const char *keys[2] = {NVS_PRESETS_KEY, NVS_CUES_KEY};
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
        fx.group = constrain((int)(o["group"] | 0), 0, GROUP_COUNT);
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
        p.fadeMs     = o["fade"].is<int>() ? constrain((int)o["fade"], 0, 10000) : -1;
        p.fxWidth    = constrain((int)(o["w"] | 1), 1, 8);
        p.fxIntensity = constrain((int)(o["i"] | 50), 0, 100);
        if ((o["v2"] | false) || o["groups"].is<JsonArray>()) {
            p.hasGroups = true;   // fehlende Gruppen/Palette = nicht benutzt (custom/on = false)
            groupsFromJson(o["groups"].as<JsonArray>(), p.groups);
            paletteFromJson(o["palette"].as<JsonObject>(), p.palette);
        }
        state.presets.push_back(p);
    }

    // Gruppen, Palette
    groupsFromJson(doc["groups"].as<JsonArray>(), state.groups);
    paletteFromJson(doc["palette"].as<JsonObject>(), state.palette);

    // Nebel
    JsonObject fog = doc["fog"].as<JsonObject>();
    state.fogChannel     = constrain((int)(fog["channel"]     | 0), 0, 512);
    state.fogLevel       = fog["level"]       | 255;
    state.fogOnDrop      = fog["onDrop"]      | false;
    state.fogDropSec     = constrain((int)(fog["dropSec"]     | 3), 1, 30);
    state.fogIntervalMin = constrain((int)(fog["intervalMin"] | 0), 0, 120);
    state.fogIntervalSec = constrain((int)(fog["intervalSec"] | 5), 1, 30);

    // Übergänge, Effekt-Parameter, Lautstärke-Helligkeit
    state.fadeMs        = constrain((int)(doc["fadeMs"] | 600), 0, 10000);
    state.fxWidth       = constrain((int)(doc["fxWidth"] | 1), 1, 8);
    state.fxIntensity   = constrain((int)(doc["fxIntensity"] | 50), 0, 100);
    state.levelDim      = doc["levelDim"] | false;
    state.levelDimDepth = constrain((int)(doc["levelDimDepth"] | 50), 0, 100);

    // Moving-Head-Positionen
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

    // Zeitplan, Display
    JsonObject sched = doc["schedule"].as<JsonObject>();
    state.scheduleOn     = sched["on"]     | false;
    state.scheduleStart  = sched["start"]  | "18:00";
    state.scheduleEnd    = sched["end"]    | "02:00";
    state.schedulePreset = sched["preset"] | "";
    state.displayOffMin  = constrain((int)(doc["displayOffMin"] | 15), 0, 240);

    // Standby
    state.standbyMin    = constrain((int)(doc["standbyMin"] | 0), 0, 240);
    state.standbyPreset = doc["standbyPreset"] | "";

    // Cue-Liste
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

    // Mikrofon-Kalibrierung
    JsonObject cal = doc["calibration"].as<JsonObject>();
    state.calibrated = cal["done"]      | false;
    state.gateKick   = cal["gateKick"]  | 0.0;
    state.gateSnare  = cal["gateSnare"] | 0.0;
    state.gateHihat  = cal["gateHihat"] | 0.0;
    state.refKick    = cal["refKick"]   | 180000.0;
    state.refSnare   = cal["refSnare"]  | 190000.0;
    state.refHihat   = cal["refHihat"]  | 170000.0;
    state.refLevel   = cal["refLevel"]  | 5000.0;

    // Effekt "retro" heißt jetzt "lightorgan" – gespeicherte Namen umstellen
    auto rename = [](String &e) { if (e == "retro") e = "lightorgan"; };
    rename(state.currentEffect);
    for (auto &p : state.presets) {
        rename(p.effect);
        for (auto &g : p.groups) rename(g.effect);
    }
    for (auto &g : state.groups) rename(g.effect);

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
