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

    // Gruppe: 0 = keine (folgt dem Haupteffekt), 1…GROUP_COUNT
    uint8_t group = 0;
};

// Fixture-Gruppen: eigene Effekte/Farben für Teile der Lampen
#define GROUP_COUNT 4
struct GroupSettings {
    String  name;
    bool    custom = false;     // false = folgt dem Haupteffekt
    String  effect = "static";
    uint8_t r = 255, g = 0, b = 0;
};

// Farbpalette, die Chase, Disco, Fade & Co. statt fester Farben verwenden
#define PALETTE_MAX 8
struct Palette {
    bool    on = false;
    uint8_t size = 3;           // 2…PALETTE_MAX
    uint8_t c[PALETTE_MAX][3] = {
        {255, 0, 0}, {0, 0, 255}, {255, 0, 255}, {0, 255, 255},
        {255, 255, 0}, {0, 255, 0}, {255, 128, 0}, {255, 255, 255}
    };
};

// Cue-Liste: feste Abfolge von Presets
#define MAX_CUES 32
struct Cue {
    String preset;
    int    duration = 16;       // in Beats oder Sekunden
    bool   beats = true;
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

    // Ab dieser Version auch Gruppen + Palette (ältere Presets lassen sie unverändert)
    bool          hasGroups = false;
    GroupSettings groups[GROUP_COUNT];
    Palette       palette;

    // Übergangszeit beim Aufrufen (-1 = globale Einstellung) und Effekt-Parameter
    int     fadeMs = -1;
    uint8_t fxWidth = 1;
    uint8_t fxIntensity = 50;

    // Energie für die intelligente Auto-Show: 0 = egal, 1 = ruhig, 2 = mittel, 3 = wild
    uint8_t energy = 0;
};

// Energie der Musik (für die intelligente Auto-Show)
enum MusicEnergy { ENERGY_ANY = 0, ENERGY_CALM = 1, ENERGY_MEDIUM = 2, ENERGY_WILD = 3 };

// Eigenes Lauflicht: Raster aus "width" Positionen × "steps" Schritten.
// Fixture i nimmt Position i % width. Farben als 0xRRGGBB.
#define USER_CHASES      4
#define USER_CHASE_WIDTH 8
#define USER_CHASE_STEPS 16
struct UserChase {
    String  name;                 // leer = Platz frei
    uint8_t width = 4;
    uint8_t steps = 4;
    bool    smooth = false;       // zwischen den Schritten überblenden
    uint32_t cells[USER_CHASE_STEPS][USER_CHASE_WIDTH] = {{0}};
};

// Synchronisation mehrerer Controller per ESP-NOW
enum SyncRole { SYNC_OFF = 0, SYNC_MASTER = 1, SYNC_SLAVE = 2 };

// Gespeicherte Moving-Head-Position (pro Head Pan/Tilt, zugeordnet über den Namen)
#define MAX_POSITIONS 8
struct HeadPos { String fixture; uint8_t pan; uint8_t tilt; };
struct MhPosition {
    String name;
    std::vector<HeadPos> heads;
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

    // Übergänge und Effekt-Parameter
    int     fadeMs = 600;               // Übergangszeit bei Effekt-/Farb-/Preset-Wechsel (0 = hart)
    volatile int nextFadeMs = -1;       // einmalig für den nächsten Übergang (Preset), -1 = fadeMs
    uint8_t fxWidth = 1;                // Breite: leuchtende Lampen bei Chase/Bounce (1…8)
    uint8_t fxIntensity = 50;           // Intensität 0…100 (Flackern, Dichte, Wellenabstand …)

    // Helligkeit folgt der Lautstärke
    bool    levelDim = false;
    uint8_t levelDimDepth = 50;         // wie stark (0…100 %)

    // Moving-Head-Positionen
    std::vector<MhPosition> positions;

    // Zeitplan (Uhrzeit per NTP aus dem Internet)
    bool    scheduleOn = false;
    String  scheduleStart = "18:00";    // ab hier Licht an
    String  scheduleEnd = "02:00";      // ab hier Licht aus (darf über Mitternacht gehen)
    String  schedulePreset = "";        // beim Einschalten aufrufen (leer = keins)
    volatile bool scheduleDark = false;     // gerade außerhalb der Zeit → dunkel
    volatile bool scheduleOverride = false; // per UI vorzeitig eingeschaltet (bis zum nächsten Wechsel)
    volatile bool timeValid = false;        // Uhrzeit vom NTP-Server erhalten

    // Display-Schutz
    int     displayOffMin = 15;         // Display aus nach X Min ohne Musik/Bedienung (0 = nie)
    volatile unsigned long lastUserMs = 0;  // letzte Bedienung (Web oder Taster)

    // Für "nächstes Preset" (Taster)
    volatile int lastPresetIndex = -1;
    // Gerade aktives Preset (für die Anzeige); -1, sobald Effekt/Farbe von Hand geändert werden
    volatile int activePreset = -1;

    // Lampe finden: Fixture blinkt weiß bis identifyUntil
    volatile int identifyIndex = -1;
    volatile unsigned long identifyUntil = 0;

    // Kanaltest: einzelne DMX-Kanäle von Hand setzen (endet automatisch nach 2 Min ohne Änderung)
    bool    testMask[512] = {false};
    uint8_t testVal[512] = {0};
    volatile unsigned long testUntil = 0;

    // Eigene Lauflichter
    UserChase userChases[USER_CHASES];

    // Synchronisation (ESP-NOW)
    uint8_t syncRole = SYNC_OFF;
    uint8_t syncGroup = 1;               // nur Controller derselben Gruppe hören aufeinander
    volatile bool syncReceiving = false; // Slave: bekommt gerade Daten vom Master

    // Passwortschutz für die Web-Oberfläche (abwählbar)
    bool    authOn = false;
    String  authUser = "admin";
    String  authPass = "";

    // MQTT / Home Assistant
    bool    mqttOn = false;
    String  mqttHost = "";
    int     mqttPort = 1883;
    String  mqttUser = "";
    String  mqttPass = "";
    String  mqttTopic = "dmxcontroller";
    volatile bool mqttConnected = false;

    // Presets + Auto-Show
    std::vector<Preset> presets;
    bool autoShow = false;      // Presets automatisch durchschalten
    bool autoShowSmart = false; // … passend zur Energie der Musik
    volatile uint8_t musicEnergy = ENERGY_CALM; // aktuelle Einschätzung
    int  showBeats = 32;        // … alle so viele Beats

    // Gruppen + Palette
    GroupSettings groups[GROUP_COUNT];
    Palette palette;

    // Nebelmaschine (DMX-Kanal, 0 = keine)
    int     fogChannel = 0;
    uint8_t fogLevel = 255;
    bool    fogOnDrop = false;
    int     fogDropSec = 3;
    int     fogIntervalMin = 0;     // 0 = aus
    int     fogIntervalSec = 5;
    volatile unsigned long fogUntil = 0;        // automatischer Stoß bis …
    volatile unsigned long manualFogUntil = 0;  // Nebel-Knopf (Keepalive wie Strobe)

    // Standby: ohne Musik auf eine ruhige Szene wechseln
    int     standbyMin = 0;         // 0 = aus
    String  standbyPreset = "";     // leer = nur dimmen
    volatile bool standbyActive = false;
    volatile bool userTouched = false;          // Bedienung in der UI → Standby-Rückkehr verwerfen

    // Cue-Liste
    std::vector<Cue> cues;
    bool cueLoop = true;
    volatile bool cueRunning = false;
    volatile bool cueRestart = false;           // Start von vorn angefordert
    volatile bool cueNext = false;              // nächster Cue angefordert
    volatile int  cueIndex = -1;

    // Mikrofon-Kalibrierung (Einmessen)
    bool   calibrated = false;
    double gateKick = 0, gateSnare = 0, gateHihat = 0;            // Rauschschwellen
    double refKick = 180000, refSnare = 190000, refHihat = 170000; // "laut" für Light Organ & Co.
    double refLevel = 5000;                                        // Pegel für Vollausschlag

    // Spektrum für die Web-UI (16 Bänder, 0…255)
    volatile uint8_t spectrum[16] = {0};

    // Kicks direkt aus der Musik (unabhängig von Tap-Tempo), für Standby
    volatile uint32_t audioKickCount = 0;
    volatile uint32_t snareCount = 0;       // erkannte Snares (für den Snare-Blitz)
    volatile unsigned long lastAudioKickMs = 0;

    // Netzwerk-Eingang: Steuerung durch PC-Software per Art-Net / sACN
    bool netInput = false;
    int  netUniverse = 1;       // wie in QLC+ angezeigt (Art-Net = Universum-1, sACN = Universum)
    bool strobeOnFastBass = false;
    int audioThreshold = 45; // Standardwert
};

extern ControllerState state;

// --- Task-Aufteilung auf die zwei Cores ---
// Core 0: WLAN-Stack, Audio (FFT), Display
// Core 1: DMX-Senden, Webserver (AsyncTCP), Effekte, loop() (WLAN-Verwaltung, OTA, WebSocket)
#define CORE_AUDIO      0
#define CORE_LIGHT      1
#define PRIO_DMX        5   // muss pünktlich senden, rechnet aber kaum
#define PRIO_AUDIO      3   // darf nicht hinter das Mikrofon zurückfallen
#define PRIO_EFFECT     2
#define PRIO_DISPLAY    1

// Laufzeit-Messung pro Task (µs), für /api/load
struct LoadStats {
    volatile uint32_t audioUs = 0, audioMaxUs = 0;     // Rechenzeit pro Audio-Frame
    volatile uint32_t effectUs = 0, effectMaxUs = 0;   // Rechenzeit pro Effekt-Bild
    volatile uint32_t dmxUs = 0, dmxMaxUs = 0;         // Sendezeit pro DMX-Bild
    volatile uint32_t displayUs = 0;                   // Zeichnen + Übertragen
    volatile uint32_t dmxFrames = 0;                   // gesendete DMX-Bilder (für Bildrate)
    TaskHandle_t audioTask = nullptr, effectTask = nullptr, dmxTask = nullptr, displayTask = nullptr;
};
extern LoadStats loadStats;

// Gleitender Mittelwert + Maximum (Maximum klingt langsam ab)
inline void recordLoad(volatile uint32_t &avg, volatile uint32_t &mx, uint32_t us) {
    avg = (avg * 15 + us) / 16;
    mx = (us > mx) ? us : (mx - mx / 64);
}

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

// Passwortschutz wirklich aktiv? (eingeschaltet UND ein Passwort gesetzt – sonst kein Aussperren)
inline bool authActive() { return state.authOn && state.authPass.length() >= 4; }

// Tap-Tempo: einen Tap registrieren (Web-UI oder Taster), gibt die BPM zurück
int registerTap();

// FixtureDefinition <-> JSON (gemeinsam für Config und Web-API)
void fixtureDefToJson(JsonObject t, const FixtureDefinition &d);
FixtureDefinition fixtureDefFromJson(JsonObject t);

// Preset aufrufen (Aufrufer hält den StateLock). persist=false für Auto-Show/Drop,
// damit nicht ständig in den Flash geschrieben wird.
void applyPreset(int index, bool persist);
void applyPresetData(const Preset &p, bool persist);
// Aktuellen Zustand als Preset erfassen (Effekt, Farbe, Speed, Master, Gruppen, Palette)
Preset capturePreset(const String &name);

// Gruppen/Palette als JSON für die Web-API
void groupsToJsonPublic(JsonArray arr);
void paletteToJsonPublic(JsonObject o);

// Effekt, der für ein Fixture gerade gilt (Gruppen-Effekt oder Haupteffekt)
const String& effectiveEffect(int fixtureIndex);

// Farbe als "#rrggbb" bzw. aus Hex lesen
String colorToHex(uint8_t r, uint8_t g, uint8_t b);
void   hexToColor(const String &hex, uint8_t &r, uint8_t &g, uint8_t &b);
int  findPreset(const String &name);   // -1, wenn nicht gefunden

// Schreibt die gespeicherten Zusatzkanäle (Gobo …) aller Fixtures in den DMX-Puffer
void applyFixtureExtras();

// WICHTIG: Rückgabe der Definition passend zum String-Typ der Fixture
const FixtureDefinition* getFixtureDef(const String &typeName);

// Prototyp für die DMX Ansteuerung (muss in dmx.cpp definiert sein)
void setFixtureMovement(int index, uint8_t pan, uint8_t tilt);
