#include <Arduino.h>
#include <LittleFS.h>
#include "state.h"
#include "dmx.h"
#include "audio.h"
#include "web.h"
#include "effects.h"
#include "wifi.h"
#include "ota.h"
#include "netinput.h"
#include "display.h"
#include "buttons.h"
#include "schedule.h"
#include "sync.h"
#include "mqtt.h"


// Scheduled restart (e.g. after saving the Wi-Fi credentials)
static volatile unsigned long restartAt = 0;

void scheduleRestart(unsigned long delayMs) {
    unsigned long t = millis() + delayMs;
    restartAt = t ? t : 1;
}

// Auto show: switches to the next preset every showBeats beats.
// Without detected beats 120 BPM are assumed (0.5 s per beat).
// Called in the effect task with the StateLock held.
// Estimate the energy of the music: volume (smoothed) + kicks per second (last 8 s).
// A new value only applies once it has been stable for 2 s (no flip-flopping).
static void updateMusicEnergy() {
    static float lvl = 0;
    static unsigned long kickTimes[32] = {0};
    static int kickPos = 0;
    static uint32_t lastKicks = 0;
    static uint8_t candidate = ENERGY_CALM;
    static unsigned long candidateSince = 0;

    unsigned long now = millis();
    uint32_t kc = state.audioKickCount;
    while (lastKicks != kc) { lastKicks++; kickTimes[kickPos] = now; kickPos = (kickPos + 1) % 32; }
    int recent = 0;
    for (unsigned long t : kickTimes) if (t && now - t < 8000) recent++;
    float rate = recent / 8.0f;

    float l = constrain(state.level / max(1.0, state.refLevel), 0.0, 2.0);
    lvl += (l - lvl) * 0.01f;   // ~2 s smoothing at 50 frames/s

    uint8_t e = ENERGY_MEDIUM;
    if (rate < 0.5f || lvl < 0.25f) e = ENERGY_CALM;
    else if (lvl > 0.6f && rate >= 1.6f) e = ENERGY_WILD;

    if (e != candidate) { candidate = e; candidateSince = now; }
    else if (now - candidateSince > 2000) state.musicEnergy = e;
}

// Pick a preset matching the energy: same energy, else "any", else any preset.
// The currently running preset only if there is no alternative.
static int pickPresetForEnergy(uint8_t energy, int current) {
    std::vector<int> pool;
    for (int pass = 0; pass < 3 && pool.empty(); pass++) {
        for (int i = 0; i < (int)state.presets.size(); i++) {
            uint8_t pe = state.presets[i].energy;
            bool match = (pass == 0) ? pe == energy : (pass == 1) ? pe == ENERGY_ANY : true;
            if (match && i != current) pool.push_back(i);
        }
    }
    if (pool.empty()) return current;
    return pool[random(0, pool.size())];
}

static void handleAutoShow(bool newDrop) {
    static bool wasOn = false;
    static uint32_t startBeat = 0;
    static unsigned long startMs = 0;
    static int index = -1;
    static uint8_t lastEnergy = ENERGY_CALM;

    unsigned long now = millis();
    // The auto show pauses during standby or a running cue list
    bool on = state.autoShow && state.presets.size() >= 2 && !state.standbyActive && !state.cueRunning;
    if (!on || !wasOn) {
        // (Re)start: count from now on
        startBeat = state.beatCount;
        startMs = now;
        wasOn = on;
        return;
    }

    uint32_t beatsSince = state.beatCount - startBeat;
    bool due = (state.bpm >= 30)
        ? (beatsSince >= (uint32_t)state.showBeats)
        : (now - startMs >= (unsigned long)state.showBeats * 500UL);

    if (state.autoShowSmart) {
        // Smart: also switch immediately when the energy changes (after at least 8 beats)
        // or a drop comes (then a "wild" preset)
        uint8_t energy = newDrop ? ENERGY_WILD : state.musicEnergy;
        bool energyChanged = energy != lastEnergy && beatsSince >= 8;
        if (!due && !energyChanged && !newDrop) return;
        lastEnergy = energy;
        index = pickPresetForEnergy(energy, index);
    } else {
        if (!due) return;
        index = (index + 1) % state.presets.size();
    }
    applyPreset(index, false); // don't save → spares the flash
    startBeat = state.beatCount;
    startMs = now;
}

// Cue list: presets in a fixed order, each cue with its own duration
// (beats or seconds; without detected beats a beat counts as 0.5 s).
static void handleCues() {
    static uint32_t startBeat = 0;
    static unsigned long startMs = 0;

    if (!state.cueRunning || state.cues.empty()) {
        state.cueRunning = false;
        state.cueIndex = -1;
        return;
    }

    unsigned long now = millis();
    int next = -1;
    if (state.cueRestart) {
        state.cueRestart = false;
        next = 0;
    } else if (state.cueNext) {
        state.cueNext = false;
        next = state.cueIndex + 1;
    } else if (!state.standbyActive && state.cueIndex >= 0 && state.cueIndex < (int)state.cues.size()) {
        const Cue &c = state.cues[state.cueIndex];
        bool due = c.beats
            ? (state.bpm >= 30 ? state.beatCount - startBeat >= (uint32_t)c.duration
                               : now - startMs >= (unsigned long)c.duration * 500UL)
            : now - startMs >= (unsigned long)c.duration * 1000UL;
        if (due) next = state.cueIndex + 1;
    }
    if (next < 0) return;

    if (next >= (int)state.cues.size()) {
        if (!state.cueLoop) {
            state.cueRunning = false;
            state.cueIndex = -1;
            return;
        }
        next = 0;
    }
    state.cueIndex = next;
    applyPreset(findPreset(state.cues[next].preset), false);
    startBeat = state.beatCount;
    startMs = now;
}

// Standby: if no music plays for standbyMin minutes (no kicks from the microphone),
// the controller switches to the standby preset or dims to 20 %.
// When 4 kicks arrive within 4 s again, the previous state is restored.
// If someone uses the UI during standby, their setting stays (no jumping back).
static void handleStandby() {
    static Preset before;
    static uint32_t lastKickCount = 0;
    static unsigned long kickTimes[4] = {0};
    static int kickPos = 0;
    static int kicksSinceStandby = 0;
    static int dimmedMaster = -1;   // Master in "dim only" standby, otherwise -1

    unsigned long now = millis();
    uint32_t kc = state.audioKickCount;
    if (kc != lastKickCount) {
        lastKickCount = kc;
        kickTimes[kickPos] = now;
        kickPos = (kickPos + 1) % 4;
        kicksSinceStandby++;
    }

    if (state.userTouched) {
        state.userTouched = false;
        if (state.standbyActive) {
            // Interaction wins, no restore – only undo the dimming,
            // unless the master itself was just changed
            if (dimmedMaster >= 0 && state.master == dimmedMaster) state.master = before.master;
            state.standbyActive = false;
        }
    }

    bool enabled = state.standbyMin > 0 && !netInputActive();
    if (!state.standbyActive) {
        if (!enabled) return;
        if (now - state.lastAudioKickMs < (unsigned long)state.standbyMin * 60000UL) return;

        before = capturePreset("");
        int p = findPreset(state.standbyPreset);
        if (p >= 0) {
            applyPreset(p, false);
        } else {
            state.master = state.master * 20 / 100; // dim only
            state.fadeRequest = true;
        }
        dimmedMaster = (p >= 0) ? -1 : state.master;
        state.standbyActive = true;
        kicksSinceStandby = 0;
        Serial.println("Standby: keine Musik → ruhige Szene");
        return;
    }

    // Wake up: 4 kicks within 4 s (or standby disabled)
    unsigned long oldest = kickTimes[kickPos]; // oldest of the last 4
    bool music = kicksSinceStandby >= 4 && now - oldest < 4000;
    if (music || !enabled) {
        applyPresetData(before, false);
        state.standbyActive = false;
        Serial.println("Standby beendet: Musik erkannt");
    }
}

// Fog: automatic bursts on drops and at intervals (output in updateDMX)
static void handleFog(bool newDrop) {
    static unsigned long lastInterval = 0;
    if (state.fogChannel <= 0) return;

    unsigned long now = millis();
    if (newDrop && state.fogOnDrop) {
        unsigned long until = now + state.fogDropSec * 1000UL;
        if ((long)(until - state.fogUntil) > 0) state.fogUntil = until;
    }
    if (state.fogIntervalMin > 0) {
        if (now - lastInterval >= (unsigned long)state.fogIntervalMin * 60000UL) {
            lastInterval = now;
            state.fogUntil = now + state.fogIntervalSec * 1000UL;
        }
    } else {
        lastInterval = now; // after enabling, wait a full interval first
    }
}

static bool isAudioEffect(const String &fx) {
    return fx == "disco" || fx == "disco2" || fx == "lightorgan" || fx == "retro" ||
           fx == "pump" || fx == "snareflash" || fx == "basswave";
}

// Compute and send one frame of the normal effect control.
// Returns the wait time until the next frame. Called with the StateLock held.
static int runEffectFrame(bool dropReaction, bool &wasStrobing) {
    // Split fixtures: main effect (slot 0) and groups with their own effect (slot 1…4)
    static std::vector<int> sets[GROUP_COUNT + 1];
    for (auto &v : sets) v.clear();
    for (int i = 0; i < (int)state.fixtures.size(); i++) {
        uint8_t g = state.fixtures[i].group;
        bool own = g >= 1 && g <= GROUP_COUNT && state.groups[g - 1].custom;
        sets[own ? g : 0].push_back(i);
    }

    // --- EFFECT LOGIC ---
    const String &fx = state.currentEffect;
    bool hasEffect = (fx != "none" && fx != "");
    bool strobing = (long)(state.manualStrobeUntil - millis()) > 0;
    bool audioFx = hasEffect && isAudioEffect(fx);

    if (strobing) {
        // Strobe button pressed → overrides the effect (hard, no transition)
        cancelFade();
        effectManualStrobe();
    } else if (dropReaction) {
        // Drop detected → configured reaction overrides the effect
        cancelFade();
        effectDropReaction(state.dropAction, millis() - state.dropStartMs);
    } else {
        if (hasEffect) runEffectOn(fx, sets[0], state.colorR, state.colorG, state.colorB, 0);
        for (int g = 1; g <= GROUP_COUNT; g++) {
            const GroupSettings &G = state.groups[g - 1];
            if (sets[g].empty() || G.effect == "none") continue;
            runEffectOn(G.effect, sets[g], G.r, G.g, G.b, g);
            audioFx |= isAudioEffect(G.effect);
        }
        if (!hasEffect && wasStrobing) {
            // Without an effect, go dark after the strobe instead of staying white
            for (int i : sets[0]) setFixtureColor(i, 0, 0, 0, 0);
        }
    }
    wasStrobing = strobing;

    // Find lamp: selected fixture flashes white (on top of any effect)
    int idf = state.identifyIndex;
    if (idf >= 0 && idf < (int)state.fixtures.size() && (long)(state.identifyUntil - millis()) > 0) {
        uint8_t v = ((millis() / 250) % 2) ? 255 : 0;
        setFixtureColor(idf, v, v, v, v);
    }

    // Always send: DMX devices expect continuous data (and the fog channel must go out)
    updateDMX();

    // Dynamic delay
    // Send at ~50 fps during a transition so it looks smooth
    // Also 20 ms when on beat so the steps land exactly on the beat
    bool fast = audioFx || isFading() || state.beatSync || dropReaction;
    return strobing ? 10 : fast ? 20 : (state.timerSpeed / 20);
}

// 2. Effect task (core 1)
void effectTask(void *param) {
    uint32_t lastBeatCount = state.beatCount;
    uint32_t lastDropCount = state.dropCount;
    bool wasStrobing = false;
    bool wasNet = false;
    static uint8_t netFrame[512];

    while (true) {
        int delayTime;
        {
            StateLock lock;

            // --- SAVE CHECK ---
            // Checks every few milliseconds whether nothing was changed in the web interface 
            // for 5 s and then saves to flash once.
            if (!state.otaActive) handleAutoSave(); 

            uint32_t t0 = micros(); // Measure the compute time of the frame

            // --- BEAT ---
            // The audio task counts kicks up; that way no beat is lost
            // and none is evaluated twice
            uint32_t beats = state.beatCount;
            state.beat = (beats != lastBeatCount);
            lastBeatCount = beats;

            // --- SYNC: slave takes over beat, effect & co. from the master ---
            applySyncFromMaster();

            // --- DROP ---
            uint32_t drops = state.dropCount;
            bool newDrop = (drops != lastDropCount);
            lastDropCount = drops;
            if (newDrop && state.dropAction == DROP_PRESET) {
                applyPreset(findPreset(state.dropPreset), false);
            }
            bool dropReaction = state.dropCount > 0 && state.dropAction != DROP_PRESET &&
                                millis() - state.dropStartMs < DROP_HOLD_MS;

            // --- STANDBY, CUE LIST, AUTO SHOW, FOG ---
            handleStandby();
            handleCues();
            updateMusicEnergy();
            handleAutoShow(newDrop);
            handleFog(newDrop);

            // --- PC CONTROL (Art-Net / sACN) ---
            // When network data arrives it is output 1:1; effects pause
            if (netInputActive()) {
                // The strobe button also has priority during PC control
                if ((long)(state.manualStrobeUntil - millis()) > 0) {
                    effectManualStrobe();
                    updateDMX();
                    delayTime = 10;
                } else {
                    updateDMXFromNetwork(netFrame, copyNetDmx(netFrame));
                    delayTime = 20;
                }
                wasNet = true;
            } else {
                if (wasNet) {
                    wasNet = false;
                    state.fadeRequest = true; // smoothly back to effect control
                }
                delayTime = runEffectFrame(dropReaction, wasStrobing);
            }
            recordLoad(loadStats.effectUs, loadStats.effectMaxUs, micros() - t0);
        }
        vTaskDelay(pdMS_TO_TICKS(max(10, delayTime)));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    
    if (!LittleFS.begin(true)) Serial.println("LittleFS Error!");

    initState(); // Loads all saved values at startup

    // Escape hatch: tap button held at power-on → password protection off
    pinMode(BTN_TAP_PIN, INPUT_PULLUP);
    delay(20);
    if (digitalRead(BTN_TAP_PIN) == LOW && state.authOn) {
        state.authOn = false;
        saveConfig();
        Serial.println("Passwortschutz per Tap-Taster abgeschaltet");
    }

    setupDisplay(); // shows "starting …" while Wi-Fi connects (language from the config)
    updateDMXChannelCount();
    setupDMX();
    setupWiFi(); 
    setupWeb(); 
    setupNetInput(); // Art-Net / sACN, if enabled
    setupSchedule(); // Time via NTP for the schedule
    setupButtons();  // Buttons / footswitch
    setupSync();     // ESP-NOW (several controllers in sync)
    setupMqtt();     // MQTT / Home Assistant (own task)

    // Core 0: audio (+ Wi-Fi stack, display)
    xTaskCreatePinnedToCore(taskAudio, "AudioTask", 10240, NULL, PRIO_AUDIO, &loadStats.audioTask, CORE_AUDIO);
    startDisplayTask();

    // Core 1: effects (+ DMX output, web server, loop). Also includes the auto-save.
    xTaskCreatePinnedToCore(effectTask, "effectTask", 6144, NULL, PRIO_EFFECT, &loadStats.effectTask, CORE_LIGHT);

    Serial.println("System ready.");
}

void loop() {
    // Effects and audio run in their own FreeRTOS tasks;
    // the loop (core 1) handles Wi-Fi, OTA updates, buttons, schedule, WebSocket and scheduled restarts
    handleWiFi();
    handleOTA();
    handleButtons();
    handleSchedule();
    handleSync();
    handleLiveStatus();

    if (restartAt && (long)(millis() - restartAt) >= 0) {
        StateLock lock; // waits until a running save has finished
        Serial.println("Neustart …");
        delay(100);
        ESP.restart();
    }
    vTaskDelay(pdMS_TO_TICKS(10)); 
}
