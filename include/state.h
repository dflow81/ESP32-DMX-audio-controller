#pragma once
#include <Arduino.h>
#include <vector>
#include <map>

struct FixtureDefinition {
    int channels;
    int offsetDimmer;
    int offsetR;
    int offsetG;
    int offsetB;
    int offsetW;
    int offsetPan;
    int offsetTilt;
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
};

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
    int numChannels = 0;
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
    bool  beat = false;
    bool  drop = false;
    float bpm = 120.0f; // Standardwert für BPM
    double level = 0.0;     
    double gain = 1.0;      
    bool autoGain = true;

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
    bool strobeOnFastBass = false;
     int audioThreshold = 45; // Standardwert
};

extern ControllerState state;

// Funktions-Prototypen
void initState();
void loadDefaultFixtureTypes();
void updateDMXChannelCount();
void saveConfig();
void saveDefaultConfig();
void loadConfig();
void markConfigDirty();
void handleAutoSave();

// WICHTIG: Rückgabe der Definition passend zum String-Typ der Fixture
const FixtureDefinition* getFixtureDef(const String &typeName);

// Prototyp für die DMX Ansteuerung (muss in dmx.cpp definiert sein)
void setFixtureMovement(int index, uint8_t pan, uint8_t tilt);
