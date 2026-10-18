#pragma once
#include <Arduino.h>
#include <vector>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>

// Extra channels for moving heads (not changed by effects,
// but set on the moving head page and stored per fixture)
enum ExtraChannel { EXTRA_GOBO, EXTRA_COLORWHEEL, EXTRA_PRISM, EXTRA_FOCUS, EXTRA_COUNT };
extern const char* const EXTRA_NAMES[EXTRA_COUNT];       // API names: "gobo", "colorWheel", …
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
    int offsetExtra[EXTRA_COUNT]; // -1 = not present
};

struct Fixture {
    String name;
    int start;
    String type;
    
    // Limits for moving head movement (0-255)
    uint8_t panMin = 0;
    uint8_t panMax = 255;
    uint8_t tiltMin = 0;
    uint8_t tiltMax = 255;
    
    // State for BPM-synced movement
    bool isAutoMoving = false;
    float movePhase = 0.0f; // Internal counter for the oscillation (0.0 to 1.0)

    // Values of the extra channels (gobo, color wheel, prism, focus)
    uint8_t extra[EXTRA_COUNT] = {0, 0, 0, 0};

    // Group: 0 = none (follows the main effect), 1…GROUP_COUNT
    uint8_t group = 0;
};

// Fixture groups: own effects/colors for parts of the lamps
#define GROUP_COUNT 4
struct GroupSettings {
    String  name;
    bool    custom = false;     // false = follows the main effect
    String  effect = "static";
    uint8_t r = 255, g = 0, b = 0;
};

// Color palette that chase, disco, fade & co. use instead of fixed colors
#define PALETTE_MAX 8
struct Palette {
    bool    on = false;
    uint8_t size = 3;           // 2…PALETTE_MAX
    uint8_t c[PALETTE_MAX][3] = {
        {255, 0, 0}, {0, 0, 255}, {255, 0, 255}, {0, 255, 255},
        {255, 255, 0}, {0, 255, 0}, {255, 128, 0}, {255, 255, 255}
    };
};

// Cue list: fixed sequence of presets
#define MAX_CUES 32
struct Cue {
    String preset;
    int    duration = 16;       // in beats or seconds
    bool   beats = true;
};

// Reaction to a detected drop
enum DropAction { DROP_STROBE, DROP_FLASH, DROP_COLORBURST, DROP_PRESET, DROP_ACTION_COUNT };

// Movement patterns of the moving heads with BPM auto
enum MovePattern { MOVE_CIRCLE, MOVE_EIGHT, MOVE_PAN, MOVE_TILT, MOVE_RANDOM, MOVE_PATTERN_COUNT };

#define DROP_HOLD_MS 1200   // How long the drop reaction lasts

// Saved scene (preset)
struct Preset {
    String  name;
    String  effect = "static";
    uint8_t r = 255, g = 0, b = 0;
    int     timerSpeed = 500;
    int     master = 255;

    // Since this version also groups + palette (older presets leave them unchanged)
    bool          hasGroups = false;
    GroupSettings groups[GROUP_COUNT];
    Palette       palette;

    // Transition time when recalled (-1 = global setting) and effect parameters
    int     fadeMs = -1;
    uint8_t fxWidth = 1;
    uint8_t fxIntensity = 50;

    // Energy for the smart auto show: 0 = any, 1 = calm, 2 = medium, 3 = wild
    uint8_t energy = 0;
};

// Energy of the music (for the smart auto show)
enum MusicEnergy { ENERGY_ANY = 0, ENERGY_CALM = 1, ENERGY_MEDIUM = 2, ENERGY_WILD = 3 };

// Custom chase: grid of "width" positions × "steps" steps.
// Fixture i uses position i % width. Colors as 0xRRGGBB.
#define USER_CHASES      4
#define USER_CHASE_WIDTH 8
#define USER_CHASE_STEPS 16
struct UserChase {
    String  name;                 // empty = slot free
    uint8_t width = 4;
    uint8_t steps = 4;
    bool    smooth = false;       // crossfade between steps
    uint32_t cells[USER_CHASE_STEPS][USER_CHASE_WIDTH] = {{0}};
};

// Synchronization of several controllers via ESP-NOW
enum SyncRole { SYNC_OFF = 0, SYNC_MASTER = 1, SYNC_SLAVE = 2 };

// Saved moving head position (pan/tilt per head, matched by name)
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

    // DMX & fixtures
    uint8_t dmx[512];
    uint8_t dmxPreview[512];
    bool dimmable[512];          // Precomputed: channel is dimmed by the master
    int numChannels = 0;
    volatile bool otaActive = false; // OTA update/restore running → stop writing to flash
    volatile bool dmxReinit = false; // Web UI requests a restart of the DMX interface
    std::vector<Fixture> fixtures;
    std::map<String, FixtureDefinition> fixtureTypes;

    // Master & global color
    int master = 255;
    uint8_t colorR = 255;
    uint8_t colorG = 0; 
    uint8_t colorB = 0;

    // Wi-Fi
    String wifi_ssid = "";
    String wifi_pass = "";

    // Audio / beat detection
    volatile uint32_t beatCount = 0;   // Incremented by the audio task on every kick
    volatile uint32_t lastBeatMs = 0;  // Time of the last kick
    bool  beat = false;                // Effect task only: new kick since the last frame
    volatile bool drop = false;
    volatile uint32_t dropCount = 0;      // Incremented on every detected drop
    volatile unsigned long dropStartMs = 0;
    volatile float bpm = 120.0f; // Default BPM
    volatile bool bpmLocked = false;          // BPM locked via tap tempo → internal beat clock
    volatile unsigned long clockSyncMs = 0;   // Tap: align the beat clock to this point in time
    double level = 0.0;     
    bool dropEnabled = true; // Drop detection (can be turned off, e.g. for schlager music)
    uint8_t dropAction = DROP_STROBE;
    String dropPreset = "";  // Preset name for DROP_PRESET

    // Frequency band energies
    double kickEnergy = 0.0;
    double snareEnergy = 0.0;
    double hihatEnergy = 0.0;

    float audioLow = 0;
    float audioMid = 0;
    float audioHigh = 0;
    float audioLevel = 0;

    // Effect & sequence control
    String currentEffect = "static";
    Mode mode = MODE_AUDIO; 
    int chaseIndex = 0;
    int timerSpeed = 500;
    volatile unsigned long manualStrobeUntil = 0; // Strobe button: active until this millis() value
    volatile bool blackout = false;    // Blackout button: all brightness channels to 0 (not saved)
    volatile bool fadeRequest = false; // Smooth transition to the next frame (effect/color change)

    // Effects on beat: step length from BPM instead of timerSpeed
    bool  beatSync = false;
    float stepsPerBeat = 1.0f;  // 2 = half beats, 1 = every beat, 0.5 = every 2 beats

    // Moving heads
    uint8_t movePattern = MOVE_CIRCLE;
    bool    moveSpread = true;  // Heads offset (wave) instead of in sync

    // Transitions and effect parameters
    int     fadeMs = 600;               // Transition time on effect/color/preset change (0 = instant)
    volatile int nextFadeMs = -1;       // one-off for the next transition (preset), -1 = fadeMs
    uint8_t fxWidth = 1;                // Width: lit lamps for chase/bounce (1…8)
    uint8_t fxIntensity = 50;           // Intensity 0…100 (flicker, density, wave spacing …)

    // Brightness follows the volume
    bool    levelDim = false;
    uint8_t levelDimDepth = 50;         // how strongly (0…100 %)

    // Moving head positions
    std::vector<MhPosition> positions;

    // Schedule (time via NTP from the internet)
    bool    scheduleOn = false;
    String  scheduleStart = "18:00";    // lights on from here
    String  scheduleEnd = "02:00";      // lights off from here (may cross midnight)
    String  schedulePreset = "";        // recall when turning on (empty = none)
    volatile bool scheduleDark = false;     // currently outside the time window → dark
    volatile bool scheduleOverride = false; // turned on early via UI (until the next change)
    volatile bool timeValid = false;        // time received from the NTP server

    // Display protection
    int     displayOffMin = 15;         // Display off after X min without music/interaction (0 = never)
    volatile unsigned long lastUserMs = 0;  // last interaction (web or button)

    // For "next preset" (button)
    volatile int lastPresetIndex = -1;
    // Currently active preset (for display); -1 once effect/color are changed by hand
    volatile int activePreset = -1;

    // Find lamp: fixture flashes white until identifyUntil
    volatile int identifyIndex = -1;
    volatile unsigned long identifyUntil = 0;

    // Channel test: set individual DMX channels by hand (ends automatically after 2 min without change)
    bool    testMask[512] = {false};
    uint8_t testVal[512] = {0};
    volatile unsigned long testUntil = 0;

    // Custom chases
    UserChase userChases[USER_CHASES];

    // Synchronization (ESP-NOW)
    uint8_t syncRole = SYNC_OFF;
    uint8_t syncGroup = 1;               // only controllers of the same group listen to each other
    volatile bool syncReceiving = false; // Slave: currently receiving data from the master

    // Display language (the web UI remembers its language per browser;
    // the DE | EN switch also reports the choice here)
    bool    langEn = false;

    // Password protection for the web interface (optional)
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

    // Presets + auto show
    std::vector<Preset> presets;
    bool autoShow = false;      // Cycle presets automatically
    bool autoShowSmart = false; // … matching the energy of the music
    volatile uint8_t musicEnergy = ENERGY_CALM; // current estimate
    int  showBeats = 32;        // … every this many beats

    // Groups + palette
    GroupSettings groups[GROUP_COUNT];
    Palette palette;

    // Fog machine (DMX channel, 0 = none)
    int     fogChannel = 0;
    uint8_t fogLevel = 255;
    bool    fogOnDrop = false;
    int     fogDropSec = 3;
    int     fogIntervalMin = 0;     // 0 = off
    int     fogIntervalSec = 5;
    volatile unsigned long fogUntil = 0;        // automatic burst until …
    volatile unsigned long manualFogUntil = 0;  // Fog button (keepalive like strobe)

    // Standby: switch to a calm scene without music
    int     standbyMin = 0;         // 0 = off
    String  standbyPreset = "";     // empty = dim only
    volatile bool standbyActive = false;
    volatile bool userTouched = false;          // Interaction in the UI → discard the standby return

    // Cue list
    std::vector<Cue> cues;
    bool cueLoop = true;
    volatile bool cueRunning = false;
    volatile bool cueRestart = false;           // Restart from the beginning requested
    volatile bool cueNext = false;              // Next cue requested
    volatile int  cueIndex = -1;

    // Microphone calibration
    bool   calibrated = false;
    double gateKick = 0, gateSnare = 0, gateHihat = 0;            // Noise thresholds
    double refKick = 180000, refSnare = 190000, refHihat = 170000; // "loud" for light organ & co.
    double refLevel = 5000;                                        // Level for full scale

    // Spectrum for the web UI (16 bands, 0…255)
    volatile uint8_t spectrum[16] = {0};

    // Kicks directly from the music (independent of tap tempo), for standby
    volatile uint32_t audioKickCount = 0;
    volatile uint32_t snareCount = 0;       // Detected snares (for the snare flash)
    volatile unsigned long lastAudioKickMs = 0;

    // Network input: control by PC software via Art-Net / sACN
    bool netInput = false;
    int  netUniverse = 1;       // as shown in QLC+ (Art-Net = universe-1, sACN = universe)
    bool strobeOnFastBass = false;
    int audioThreshold = 45; // Default value
};

extern ControllerState state;

// --- Task distribution across the two cores ---
// Core 0: Wi-Fi stack, audio (FFT), display
// Core 1: DMX output, web server (AsyncTCP), effects, loop() (Wi-Fi management, OTA, WebSocket)
#define CORE_AUDIO      0
#define CORE_LIGHT      1
#define PRIO_DMX        5   // must send on time, but hardly computes
#define PRIO_AUDIO      3   // must not fall behind the microphone
#define PRIO_EFFECT     2
#define PRIO_DISPLAY    1

// Runtime measurement per task (µs), for /api/load
struct LoadStats {
    volatile uint32_t audioUs = 0, audioMaxUs = 0;     // Compute time per audio frame
    volatile uint32_t effectUs = 0, effectMaxUs = 0;   // Compute time per effect frame
    volatile uint32_t dmxUs = 0, dmxMaxUs = 0;         // Send time per DMX frame
    volatile uint32_t displayUs = 0;                   // Drawing + transfer
    volatile uint32_t dmxFrames = 0;                   // DMX frames sent (for the frame rate)
    TaskHandle_t audioTask = nullptr, effectTask = nullptr, dmxTask = nullptr, displayTask = nullptr;
};
extern LoadStats loadStats;

// Moving average + maximum (maximum decays slowly)
inline void recordLoad(volatile uint32_t &avg, volatile uint32_t &mx, uint32_t us) {
    avg = (avg * 15 + us) / 16;
    mx = (us > mx) ? us : (mx - mx / 64);
}

// Limits for the effect speed (prevents division by 0)
#define TIMER_SPEED_MIN 20
#define TIMER_SPEED_MAX 5000

// Protects state.fixtures, state.fixtureTypes, state.currentEffect and state.dmx
// from simultaneous access by the web server and the effect task
extern SemaphoreHandle_t stateMutex;

struct StateLock {
    StateLock()  { xSemaphoreTake(stateMutex, portMAX_DELAY); }
    ~StateLock() { xSemaphoreGive(stateMutex); }
};

// Function prototypes
void initState();
void loadDefaultFixtureTypes();
void updateDMXChannelCount();
void saveConfig();
void saveDefaultConfig();
void loadConfig();
void markConfigDirty();

// Backup: current config as JSON (without Wi-Fi credentials)
String exportConfigJson();
// Restore: validates the JSON and writes it to NVS (Wi-Fi credentials are kept).
// A restart is needed afterwards; until then nothing is saved anymore.
bool importConfigJson(const char *json, size_t len, String &error);
void handleAutoSave();

// Restarts the ESP after delayMs (executed in loop() so HTTP responses still go out)
void scheduleRestart(unsigned long delayMs);

// Is password protection really active? (enabled AND a password set – otherwise no lock-out)
inline bool authActive() { return state.authOn && state.authPass.length() >= 4; }

// Tap tempo: register a tap (web UI or button), returns the BPM
int registerTap();

// FixtureDefinition <-> JSON (shared by config and web API)
void fixtureDefToJson(JsonObject t, const FixtureDefinition &d);
FixtureDefinition fixtureDefFromJson(JsonObject t);

// Recall a preset (caller holds the StateLock). persist=false for auto show/drop,
// so the flash is not written all the time.
void applyPreset(int index, bool persist);
void applyPresetData(const Preset &p, bool persist);
// Capture the current state as a preset (effect, color, speed, master, groups, palette)
Preset capturePreset(const String &name);

// Groups/palette as JSON for the web API
void groupsToJsonPublic(JsonArray arr);
void paletteToJsonPublic(JsonObject o);

// Effect currently applying to a fixture (group effect or main effect)
const String& effectiveEffect(int fixtureIndex);

// Color as "#rrggbb" or read from hex
String colorToHex(uint8_t r, uint8_t g, uint8_t b);
void   hexToColor(const String &hex, uint8_t &r, uint8_t &g, uint8_t &b);
int  findPreset(const String &name);   // -1 if not found

// Writes the stored extra channels (gobo …) of all fixtures into the DMX buffer
void applyFixtureExtras();

// IMPORTANT: returns the definition matching the fixture's type string
const FixtureDefinition* getFixtureDef(const String &typeName);

// Prototype for DMX control (must be defined in dmx.cpp)
void setFixtureMovement(int index, uint8_t pan, uint8_t tilt);
