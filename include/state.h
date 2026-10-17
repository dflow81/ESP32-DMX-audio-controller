#pragma once
#include <Arduino.h>
#include <vector>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>

// Zusatzkanäle für Moving Heads (werden nicht von Effekten verändert,
// sondern auf der Moving-Head-Seite eingestellt und pro Fixture gespeichert)
enum ExtraChannel { EXTRA_GOBO, EXTRA_COLORWHEEL, EXTRA_PRISM, EXTRA_FOCUS, EXTRA_COUNT };
extern const char* const EXTRA_NAMES[EXTRA_COUNT];       // API-Namen: "gobo", "colorWheel", …
extern const char* const EXTRA_OFFSET_KEYS[EXTRA_COUNT]; // JSON: "offsetGobo", …

struct FixtureDefinition {
    int channels;
    int offsetDimmer;
    int offsetR;
    int offsetG;
    int offsetB;
    int offsetW;
    int offsetPan;
    int offsetTilt;
    int offsetExtra[EXTRA_COUNT]; // -1 = nicht vorhanden
};

struct Fixture {
    String name;
    int start;
    String type;
    
    // NEU: Grenzen für Moving Head Bewegung (0-255)
    uint8_t panMin = 0;
    uint8_t panMax = 255;
    uint8_t tiltMin = 0;
    uint8_t tiltMax = 255;
    
    // NEU: Status für BPM-Sync Bewegung
    bool isAutoMoving = false;
    float movePhase = 0.0f; // Interner Zähler für die Schwingung (0.0 bis 1.0)

    // Werte der Zusatzkanäle (Gobo, Farbrad, Prisma, Fokus)
    uint8_t extra[EXTRA_COUNT] = {0, 0, 0, 0};
};

// Reaktion auf einen erkannten Drop
enum DropAction { DROP_STROBE, DROP_FLASH, DROP_COLORBURST, DROP_PRESET, DROP_ACTION_COUNT };

// Bewegungsmuster der Moving Heads bei BPM Auto
enum MovePattern { MOVE_CIRCLE, MOVE_EIGHT, MOVE_PAN, MOVE_TILT, MOVE_RANDOM, MOVE_PATTERN_COUNT };

#define DROP_HOLD_MS 1200   // So lange dauert die Drop-Reaktion

// Gespeicherte Szene (Preset)
struct Preset {
    String  name;
    String  effect = "static";
    uint8_t r = 255, g = 0, b = 0;
    int     timerSpeed = 500;
    int     master = 255;
};
#define MAX_PRESETS      16
#define PRESET_NAME_MAX  24

struct ControllerState {
    enum Mode {
        MODE_STATIC,
        MODE_BEAT_CHASE,
        MODE_RAINBOW,
        MODE_AUDIO,
        MODE_RETRO 
    };

    // DMX & Fixtures
    uint8_t dmx[512];
    uint8_t dmxPreview[512];
    bool dimmable[512];          // Vorberechnet: Kanal wird vom Master gedimmt
    int numChannels = 0;
    volatile bool otaActive = false; // OTA-Update/Wiederherstellung läuft → nicht mehr in den Flash speichern
    volatile bool dmxReinit = false; // Web-UI fordert Neustart der DMX-Schnittstelle an
    std::vector<Fixture> fixtures;
    std::map<String, FixtureDefinition> fixtureTypes;

    // Master & globale Farbe
    int master = 255;
    uint8_t colorR = 255;
    uint8_t colorG = 0; 
    uint8_t colorB = 0;

    // WLAN
    String wifi_ssid = "";
    String wifi_pass = "";

    // Audio / Beat Detection
    volatile uint32_t beatCount = 0;   // Wird vom Audio-Task bei jedem Kick erhöht
    volatile uint32_t lastBeatMs = 0;  // Zeitpunkt des letzten Kicks
    bool  beat = false;                // Nur im Effekt-Task: neuer Kick seit letztem Frame
    volatile bool drop = false;
    volatile uint32_t dropCount = 0;      // Wird bei jedem erkannten Drop erhöht
    volatile unsigned long dropStartMs = 0;
    volatile float bpm = 120.0f; // Standardwert für BPM
    volatile bool bpmLocked = false;          // BPM per Tap-Tempo festgehalten → interner Beat-Takt
    volatile unsigned long clockSyncMs = 0;   // Tap: Beat-Takt auf diesen Zeitpunkt ausrichten
    double level = 0.0;     
    bool dropEnabled = true; // Drop-Erkennung (z. B. für Schlager abschaltbar)
    uint8_t dropAction = DROP_STROBE;
    String dropPreset = "";  // Preset-Name für DROP_PRESET

    // Frequenz-Energiewerte
    double kickEnergy = 0.0;
    double snareEnergy = 0.0;
    double hihatEnergy = 0.0;

    float audioLow = 0;
    float audioMid = 0;
    float audioHigh = 0;
    float audioLevel = 0;

    // Effekt- & Ablaufsteuerung
    String currentEffect = "static";
    Mode mode = MODE_AUDIO; 
    int chaseIndex = 0;
    int timerSpeed = 500;
    volatile unsigned long manualStrobeUntil = 0; // Strobe-Knopf: aktiv bis zu diesem millis()-Wert
    volatile bool blackout = false;    // Blackout-Knopf: alle Helligkeitskanäle auf 0 (wird nicht gespeichert)
    volatile bool fadeRequest = false; // Weicher Übergang zum nächsten Bild (Effekt-/Farbwechsel)

    // Effekte im Takt: Schrittweite aus BPM statt timerSpeed
    bool  beatSync = false;
    float stepsPerBeat = 1.0f;  // 2 = halbe Beats, 1 = jeder Beat, 0.5 = alle 2 Beats

    // Moving Heads
    uint8_t movePattern = MOVE_CIRCLE;
    bool    moveSpread = true;  // Heads versetzt (Welle) statt synchron

    // Presets + Auto-Show
    std::vector<Preset> presets;
    bool autoShow = false;      // Presets automatisch durchschalten
    int  showBeats = 32;        // … alle so viele Beats

    // Netzwerk-Eingang: Steuerung durch PC-Software per Art-Net / sACN
    bool netInput = false;
    int  netUniverse = 1;       // wie in QLC+ angezeigt (Art-Net = Universum-1, sACN = Universum)
    bool strobeOnFastBass = false;
    int audioThreshold = 45; // Standardwert
};

extern ControllerState state;

// Grenzen für die Effekt-Geschwindigkeit (verhindert Division durch 0)
#define TIMER_SPEED_MIN 20
#define TIMER_SPEED_MAX 5000

// Schützt state.fixtures, state.fixtureTypes, state.currentEffect und state.dmx
// vor gleichzeitigem Zugriff von Webserver und Effekt-Task
extern SemaphoreHandle_t stateMutex;

struct StateLock {
    StateLock()  { xSemaphoreTake(stateMutex, portMAX_DELAY); }
    ~StateLock() { xSemaphoreGive(stateMutex); }
};

// Funktions-Prototypen
void initState();
void loadDefaultFixtureTypes();
void updateDMXChannelCount();
void saveConfig();
void saveDefaultConfig();
void loadConfig();
void markConfigDirty();

// Backup: aktuelle Config als JSON (ohne WLAN-Zugangsdaten)
String exportConfigJson();
// Wiederherstellung: prüft das JSON und schreibt es ins NVS (WLAN-Daten bleiben).
// Danach ist ein Neustart nötig; bis dahin wird nicht mehr gespeichert.
bool importConfigJson(const char *json, size_t len, String &error);
void handleAutoSave();

// Startet den ESP nach delayMs neu (ausgeführt in loop(), damit HTTP-Antworten noch rausgehen)
void scheduleRestart(unsigned long delayMs);

// FixtureDefinition <-> JSON (gemeinsam für Config und Web-API)
void fixtureDefToJson(JsonObject t, const FixtureDefinition &d);
FixtureDefinition fixtureDefFromJson(JsonObject t);

// Preset aufrufen (Aufrufer hält den StateLock). persist=false für Auto-Show/Drop,
// damit nicht ständig in den Flash geschrieben wird.
void applyPreset(int index, bool persist);
int  findPreset(const String &name);   // -1, wenn nicht gefunden

// Schreibt die gespeicherten Zusatzkanäle (Gobo …) aller Fixtures in den DMX-Puffer
void applyFixtureExtras();

// WICHTIG: Rückgabe der Definition passend zum String-Typ der Fixture
const FixtureDefinition* getFixtureDef(const String &typeName);

// Prototyp für die DMX Ansteuerung (muss in dmx.cpp definiert sein)
void setFixtureMovement(int index, uint8_t pan, uint8_t tilt);
