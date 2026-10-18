#include <SparkFunDMX.h>
#include "dmx.h"
#include "state.h"
#include "effects.h"
#include <vector>

extern ControllerState state;

// UART2
HardwareSerial dmxSerial(2);

uint8_t txPin = 32;
uint8_t rxPin = 16;
uint8_t enPin = 33;

SparkFunDMX dmx;

// ------------------------------------------------------------
// Split: the effect task computes the frame (sentOut) and hands it over;
// a dedicated DMX task (core 1, highest priority) sends it. That way the slow
// serial output (up to ~23 ms for 512 channels) blocks neither the effects
// nor – via the StateLock – the web server.
// ------------------------------------------------------------
#define DMX_REFRESH_MS 100            // repeat at least this often without a new frame

static uint8_t sentOut[512];          // Last computed frame (also for the DMX monitor)
static int sentLen = 0;

static portMUX_TYPE frameMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t pendingFrame[512];     // Handover to the DMX task
static int pendingLen = 0;
static TaskHandle_t dmxTaskHandle = nullptr;

static int outChannels = 0;           // DMX task only: number of channels the interface currently sends

// Hand a frame to the DMX task (from the effect task)
static void publishFrame(int len) {
    sentLen = len;
    portENTER_CRITICAL(&frameMux);
    memcpy(pendingFrame, sentOut, len);
    pendingLen = len;
    portEXIT_CRITICAL(&frameMux);
    if (dmxTaskHandle) xTaskNotifyGive(dmxTaskHandle);
}

// DMX task only: adjust the number of sent channels (e.g. when a PC controls more
// channels via Art-Net than fixtures exist). Only on change.
static void ensureOutputChannels(int n) {
    n = constrain(n, 1, 512);
    if (n == outChannels) return;
    dmx.begin(dmxSerial, enPin, n);
    dmx.setComDir(DMX_WRITE_DIR);
    outChannels = n;
}

// Helper: checks whether a channel may be dimmed
// (mask is precomputed in updateDMXChannelCount())
bool isDimmableChannel(int channelIndex) {
    if (channelIndex < 0 || channelIndex >= 512) return false;
    return state.dimmable[channelIndex];
}

// Writes a channel only if the offset is valid and inside the DMX buffer
static inline void setChannel(int base, int offset, uint8_t v) {
    if (offset < 0) return;
    int ch = base + offset;
    if (ch >= 0 && ch < 512) state.dmx[ch] = v;
}

// Apply master
uint8_t applyMaster(uint8_t v) {
    return (v * state.master) / 255;
}

// Set movement
void setFixtureMovement(int index, uint8_t pan, uint8_t tilt) {
    if (index < 0 || index >= (int)state.fixtures.size()) return;

    auto &fx = state.fixtures[index];
    const FixtureDefinition* def = getFixtureDef(fx.type);
    if (!def) return;

    int base = fx.start - 1;

    setChannel(base, def->offsetPan, pan);
    setChannel(base, def->offsetTilt, tilt);
}

void setFixtureColor(int index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (index < 0 || index >= (int)state.fixtures.size()) return;

    auto &fx = state.fixtures[index];
    const FixtureDefinition* def = getFixtureDef(fx.type);
    if (!def) return;

    int base = fx.start - 1;

    setChannel(base, def->offsetDimmer, 255);
    setChannel(base, def->offsetR, r);
    setChannel(base, def->offsetG, g);
    setChannel(base, def->offsetB, b);
    setChannel(base, def->offsetW, w);
}

static void startDMXHardware() {
    // 1. Explicitly define the enable pin as output
    pinMode(enPin, OUTPUT);
    
    // 2. Set the pin HIGH manually to switch the RS485 driver to "transmit"
    digitalWrite(enPin, HIGH);
    if (state.numChannels > 512) state.numChannels = 512;
    dmxSerial.begin(DMX_BAUD, DMX_FORMAT, rxPin, txPin);
    dmx.begin(dmxSerial, enPin, max(1, state.numChannels));
    dmx.setComDir(DMX_WRITE_DIR);
    outChannels = max(1, state.numChannels);
    Serial.printf("DMX gestartet mit %d Kanälen\n", state.numChannels);
}

static void dmxTask(void *param) {
    static uint8_t frame[512];
    for (;;) {
        // Wait for a new frame – or repeat the last one after DMX_REFRESH_MS
        // (DMX devices expect continuous data)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(DMX_REFRESH_MS));

        if (state.dmxReinit) {   // Fixture config changed
            state.dmxReinit = false;
            dmxSerial.flush();
            startDMXHardware();
        }

        int len;
        portENTER_CRITICAL(&frameMux);
        len = pendingLen;
        memcpy(frame, pendingFrame, len);
        portEXIT_CRITICAL(&frameMux);

        uint32_t t0 = micros();
        // First wait until the previous frame is completely out: the library
        // switches the baud rate for the break and would otherwise mangle its end
        dmxSerial.flush();
        ensureOutputChannels(len);
        if (len > 0) dmx.writeBytes(frame, len, 1);
        dmx.update();
        recordLoad(loadStats.dmxUs, loadStats.dmxMaxUs, micros() - t0);
        loadStats.dmxFrames++;
    }
}

void setupDMX() {
    if (!dmxTaskHandle) {
        startDMXHardware();
        xTaskCreatePinnedToCore(dmxTask, "dmxTask", 4096, NULL, PRIO_DMX, &dmxTaskHandle, CORE_LIGHT);
        loadStats.dmxTask = dmxTaskHandle;
    } else {
        state.dmxReinit = true;   // The DMX task handles the restart itself
        xTaskNotifyGive(dmxTaskHandle);
    }
}

// MH
// Static variables for beat effects
static float beatDirection = 1.0f;
static float beatOffset = 0.0f;

// Scales 0…1 to the range between point A and B (also works if A > B)
static uint8_t scaleToRange(float w, uint8_t a, uint8_t b) {
    return constrain(a + (int)lroundf(w * ((int)b - (int)a)), 0, 255);
}

void updateMovements() {
    // 1st check: do we have a beat?
    if (state.bpm < 30) return; 

    // --- BEAT TRIGGER LOGIC ---
    // Trigger: with "on beat" every effect step (½ / 1 / 2 beats), otherwise every beat
    static long lastStep = -1;
    bool trigger = state.beat;
    if (state.beatSync) {
        long step = effectStepIndex();
        trigger = (step != lastStep);
        lastStep = step;
    }

    // Circle: reverse direction + phase jump
    if (trigger) {
        beatDirection *= -1.0f;          // reverse direction (1.0 or -1.0)
        beatOffset += random(10, 100) / 10.0f; // random phase jump
    }

    float bps = state.bpm / 60.0f;
    float seconds = millis() / 1000.0f;
    // Circle: one revolution per beat, direction flips on every beat
    float circlePhase = seconds * bps * TWO_PI * beatDirection;
    // Other patterns: one even cycle per bar (4 beats)
    float barPhase = seconds * bps * TWO_PI / 4.0f;

    // Random: target positions per head, change on every beat, approached smoothly
    static std::vector<float> randTargetP, randTargetT, randP, randT;
    int n = state.fixtures.size();
    if ((int)randP.size() < n) {
        randTargetP.resize(n, 0.5f); randTargetT.resize(n, 0.5f);
        randP.resize(n, 0.5f);       randT.resize(n, 0.5f);
    }
    if (state.movePattern == MOVE_RANDOM && trigger) {
        float sharedP = random(0, 1001) / 1000.0f, sharedT = random(0, 1001) / 1000.0f;
        for (int i = 0; i < n; i++) {
            randTargetP[i] = state.moveSpread ? random(0, 1001) / 1000.0f : sharedP;
            randTargetT[i] = state.moveSpread ? random(0, 1001) / 1000.0f : sharedT;
        }
    }

    float spread = state.moveSpread ? 0.8f : 0.0f; // Offset between the heads (wave)

    for (int i = 0; i < n; i++) {
        auto &fx = state.fixtures[i];
        
        // Only move heads with "BPM auto" enabled;
        // the others stay where the joystick put them
        if (!fx.isAutoMoving) continue;

        // 2nd check: only move during disco effects (main or group effect of the head)
        const String &eff = effectiveEffect(i);
        if (eff != "disco" && eff != "disco2") continue;

        const FixtureDefinition* def = getFixtureDef(fx.type);
        if (!def || def->offsetPan < 0) continue;

        float wavePan = 0.5f, waveTilt = 0.5f;
        float lp = barPhase + i * spread;

        switch (state.movePattern) {
            case MOVE_EIGHT:   // figure eight
                wavePan  = (sin(lp) + 1.0f) / 2.0f;
                waveTilt = (sin(2.0f * lp) + 1.0f) / 2.0f;
                break;
            case MOVE_PAN:     // pan left/right
                wavePan  = (sin(lp) + 1.0f) / 2.0f;
                break;
            case MOVE_TILT:    // tilt up/down
                waveTilt = (sin(lp) + 1.0f) / 2.0f;
                break;
            case MOVE_RANDOM:
                randP[i] += (randTargetP[i] - randP[i]) * 0.15f;
                randT[i] += (randTargetT[i] - randT[i]) * 0.15f;
                wavePan  = randP[i];
                waveTilt = randT[i];
                break;
            default: {         // MOVE_CIRCLE
                float cp = circlePhase + i * spread + beatOffset;
                wavePan  = (sin(cp) + 1.0f) / 2.0f;
                waveTilt = (cos(cp) + 1.0f) / 2.0f;
                break;
            }
        }

        // Scale to corner points A and B
        setFixtureMovement(i, scaleToRange(wavePan, fx.panMin, fx.panMax),
                              scaleToRange(waveTilt, fx.tiltMin, fx.tiltMax));
    }
}

// --- Smooth transitions ---
static uint8_t lastOut[512];   // Last sent frame (before blackout)
static uint8_t fadeFrom[512];  // Frame at the start of the transition
static unsigned long fadeStart = 0;
static bool fading = false;

bool isFading() { return fading || state.fadeRequest; }

void cancelFade() {
    fading = false;
    state.fadeRequest = false;
}

void updateDMX() {
    updateMovements(); // Compute movements before sending

    // Fog machine may lie behind the fixtures → output that far
    int fogIdx = state.fogChannel - 1;          // -1 = none
    int total = constrain(max(state.numChannels, state.fogChannel), 1, 512);

    unsigned long now = millis();
    static int fadeDuration = 600;
    if (state.fadeRequest) {
        state.fadeRequest = false;
        // A preset may bring its own transition time, otherwise the global one
        fadeDuration = state.nextFadeMs >= 0 ? state.nextFadeMs : state.fadeMs;
        state.nextFadeMs = -1;
        memcpy(fadeFrom, lastOut, sizeof(fadeFrom));
        fadeStart = now;
        fading = fadeDuration > 0;
    }
    float t = 1.0f;
    if (fading) {
        t = (now - fadeStart) / (float)fadeDuration;
        if (t >= 1.0f) { t = 1.0f; fading = false; }
    }

    // Brightness follows the volume: brighter fast, darker slowly
    static float loud = 1.0f;
    float lvlFactor = 1.0f;
    if (state.levelDim) {
        float lvl = constrain(state.level / max(1.0, state.refLevel) * 1.5, 0.0, 1.0);
        loud += (lvl - loud) * (lvl > loud ? 0.5f : 0.05f);
        float depth = state.levelDimDepth / 100.0f;
        lvlFactor = 1.0f - depth + depth * loud;
    }

    // Blackout or schedule "off": brightness to 0 immediately (the strobe button has priority)
    bool strobing = (long)(state.manualStrobeUntil - now) > 0;
    bool scheduleDark = state.scheduleDark && !state.scheduleOverride;
    bool dark = (state.blackout || scheduleDark) && !strobing;

    unsigned long nowFog = millis();
    bool fogOn = fogIdx >= 0 && ((long)(state.fogUntil - nowFog) > 0 || (long)(state.manualFogUntil - nowFog) > 0);

    for (int i = 0; i < total; i++) {
        uint8_t v = i < state.numChannels ? state.dmx[i] : 0;
        if (v == 0 && i < state.numChannels) v = state.dmxPreview[i];

        // IMPORTANT: apply the master only to brightness, NOT to pan/tilt
        bool dimmable = isDimmableChannel(i);
        if (dimmable) {
            v = applyMaster(v);
            if (state.levelDim) v = v * lvlFactor;
        }

        if (fading) v = fadeFrom[i] + (int)(((int)v - (int)fadeFrom[i]) * t);
        lastOut[i] = v;

        uint8_t out = (dark && dimmable) ? 0 : v;
        // Fog: own channel, independent of master, transitions and blackout
        if (i == fogIdx) out = fogOn ? state.fogLevel : 0;
        sentOut[i] = out;
    }

    // Channel test: channels set by hand take priority (raw, without master)
    if ((long)(state.testUntil - now) > 0) {
        for (int i = 0; i < 512; i++) {
            if (!state.testMask[i]) continue;
            if (i >= total) { memset(sentOut + total, 0, i - total); total = i + 1; }
            sentOut[i] = state.testVal[i];
        }
    }
    publishFrame(total);
}

void updateDMXFromNetwork(const uint8_t *data, int len) {
    len = constrain(len, 0, 512);
    int total = constrain(max(len, state.numChannels), 1, 512);
    cancelFade();

    // Fog button/automation also works during PC control (only overrides while active)
    unsigned long now = millis();
    int fogIdx = state.fogChannel - 1;
    bool fogOn = fogIdx >= 0 && ((long)(state.fogUntil - now) > 0 || (long)(state.manualFogUntil - now) > 0);
    if (fogOn) total = max(total, (int)state.fogChannel);

    bool dark = state.blackout;
    for (int i = 0; i < total; i++) {
        uint8_t v = i < len ? data[i] : 0;
        lastOut[i] = v; // so that the transition after Art-Net operation is clean
        uint8_t out = (dark && isDimmableChannel(i)) ? 0 : v;
        if (fogOn && i == fogIdx) out = state.fogLevel;
        sentOut[i] = out;
    }
    publishFrame(total);
}

int getDMXOutput(uint8_t *buf, int maxLen) {
    int n = min(sentLen, maxLen);
    memcpy(buf, sentOut, n);
    return n;
}
