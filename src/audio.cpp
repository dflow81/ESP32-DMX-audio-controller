#include <Arduino.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include "state.h"
#include "audio.h"

// ------------------------------------------------------------
// Audio / FFT setup
// ------------------------------------------------------------
#define SAMPLE_COUNT 1024
#define SAMPLE_RATE 44100

// float instead of double: the ESP32 computes float in hardware, double only in software
// (many times slower). Sums are still accumulated in double.
float vReal[SAMPLE_COUNT];
float vImag[SAMPLE_COUNT];
ArduinoFFT<float> FFT = ArduinoFFT<float>(vReal, vImag, SAMPLE_COUNT, SAMPLE_RATE);

// ------------------------------------------------------------
// Micro beat detection logic with noise gate
// ------------------------------------------------------------
#define HISTORY_SIZE 32 

struct BeatDetector {
    unsigned long lastHit = 0;
    double currentAvg = 0;

    bool detect(double energy, double multiplier, int minGap, double threshold) {
        if (energy < threshold) {
            currentAvg = (currentAvg * 0.9) + (energy * 0.1); 
            return false;
        }

        currentAvg = (currentAvg * (HISTORY_SIZE - 1) + energy) / HISTORY_SIZE;
        bool isHit = (energy > currentAvg * multiplier) && (millis() - lastHit > minGap);

        if (isHit) {
            lastHit = millis();
            currentAvg *= 1.2; 
        }
        return isHit;
    }
};

BeatDetector kickDet, snareDet, hihatDet;

unsigned long lastKickTime = 0;
unsigned long nextClockBeat = 0;   // Internal beat clock for tap tempo

// ------------------------------------------------------------
// Drop detection
// Typical drop: groove running → breakdown without kick → first strong kick.
// Second path: sudden strong energy jump.
// ------------------------------------------------------------
#define DROP_BREAK_MIN_MS    1500   // No kick may come for this long for it to count as a breakdown
#define DROP_BREAK_MAX_MS   30000   // Longer pauses = music was off, no drop
#define DROP_MIN_GROOVE_KICKS   8   // This many consecutive kicks must have run before the breakdown
#define DROP_KICK_STRENGTH    0.7   // Kick after the breakdown must have >= 70% of a normal kick
#define DROP_ENERGY_RATIO     2.0   // Short/long energy jump for the second detection path
#define DROP_LOCKOUT_MS      8000   // Minimum interval between two drops

unsigned long lastDrop = 0;
int    grooveKicks = 0;       // Consecutive kicks (gap < DROP_BREAK_MIN_MS)
int    grooveBeforeBreak = 0; // Groove length before the current breakdown
double kickAvg = 0;           // Average strength of a normal kick
double energyShort = 0;       // Energy of the last ~100 ms
double energyLong = 0;        // Energy of the last ~2-3 s

// ------------------------------------------------------------
// Helper functions
// ------------------------------------------------------------

double bandEnergy(int startBin, int endBin) {
    double sum = 0;
    for (int i = startBin; i <= endBin; i++) sum += vReal[i];
    return sum;
}

void updateKickBPM() {
    unsigned long now = millis();
    // With locked BPM (tap tempo) don't track the tempo from the music
    if (lastKickTime > 0 && !state.bpmLocked) {
        unsigned long diff = now - lastKickTime;
        if (diff > 270 && diff < 1333) { 
            float instantBPM = 60000.0f / diff;
            if (state.bpm < 10) state.bpm = instantBPM;
            else state.bpm = (state.bpm * 0.8f) + (instantBPM * 0.2f);
        }
    }
    lastKickTime = now;
}

// rawEnergy and kickEnergy are raw energies without gain so that the
// volume jump of the drop is preserved.
// sinceLastKick = time since the previous kick (before this frame)
bool detectDrop(double rawEnergy, bool kick, double kickEnergy, unsigned long sinceLastKick) {
    unsigned long now = millis();

    // Short- and long-term energy (one frame ≈ 23 ms)
    energyShort = energyShort * 0.7  + rawEnergy * 0.3;
    energyLong  = energyLong  * 0.99 + rawEnergy * 0.01;

    if (!kick) return false;

    bool afterBreak = sinceLastKick >= DROP_BREAK_MIN_MS && sinceLastKick <= DROP_BREAK_MAX_MS;
    if (afterBreak) {
        grooveBeforeBreak = grooveKicks;
        grooveKicks = 0;
    }
    grooveKicks++;

    bool kickLearned = kickAvg > 0 && grooveBeforeBreak >= DROP_MIN_GROOVE_KICKS;
    bool strongKick  = kickEnergy >= kickAvg * DROP_KICK_STRENGTH;
    double ratio     = energyShort / (energyLong + 1.0);

    // Normal kicks (not right after a breakdown) learn the kick strength
    if (!afterBreak) kickAvg = (kickAvg == 0) ? kickEnergy : kickAvg * 0.9 + kickEnergy * 0.1;

    if (afterBreak) {
        Serial.printf("Kick nach Breakdown: Pause %lums, Groove %d, Kick %.0f / Avg %.0f, Ratio %.2f\n",
                      sinceLastKick, grooveBeforeBreak, kickEnergy, kickAvg, ratio);
    }

    if (now - lastDrop < DROP_LOCKOUT_MS) return false;

    bool breakdownDrop = afterBreak && kickLearned && strongKick;
    bool energyDrop    = grooveKicks >= DROP_MIN_GROOVE_KICKS && strongKick && ratio > DROP_ENERGY_RATIO;

    if (breakdownDrop || energyDrop) {
        if (!state.dropEnabled) return false; // Disabled in the web UI
        lastDrop = now;
        grooveBeforeBreak = 0;
        Serial.printf(">>> DROP DETECTED (%s) <<<\n", breakdownDrop ? "Breakdown" : "Energie");
        return true;
    }
    return false;
}

// ------------------------------------------------------------
// Microphone calibration
// ------------------------------------------------------------
// Measured values: 0 = kick, 1 = snare, 2 = hi-hat, 3 = level
struct CalMeasure {
    double sum[4] = {0};
    double peak[4] = {0};
    int frames = 0;
    bool done = false;
};
static CalMeasure calSilence, calMusic;
static volatile int calPhase = 0;
static volatile unsigned long calEndMs = 0;
static unsigned long calDurationMs = 1;

void startCalibration(int phase, unsigned long durationMs) {
    CalMeasure &m = (phase == CAL_SILENCE) ? calSilence : calMusic;
    m = CalMeasure();
    calDurationMs = max(1UL, durationMs);
    calEndMs = millis() + durationMs;
    calPhase = phase;
}

void cancelCalibration() { calPhase = 0; }

// Called by the audio task for every frame
static void calibrationFrame(const double v[4]) {
    int phase = calPhase;
    if (!phase) return;
    CalMeasure &m = (phase == CAL_SILENCE) ? calSilence : calMusic;
    for (int i = 0; i < 4; i++) {
        m.sum[i] += v[i];
        if (v[i] > m.peak[i]) m.peak[i] = v[i];
    }
    m.frames++;
    if ((long)(millis() - calEndMs) >= 0) {
        m.done = m.frames > 0;
        calPhase = 0;
    }
}

void calibrationStatus(JsonObject o) {
    o["phase"]    = calPhase;
    long left     = (long)(calEndMs - millis());
    o["progress"] = calPhase ? constrain(100 - left * 100 / (long)calDurationMs, 0L, 100L) : 0;
    o["silence"]  = calSilence.done;
    o["music"]    = calMusic.done;
    o["calibrated"] = state.calibrated;
}

bool applyCalibration(String &message) {
    if (!calSilence.done && !calMusic.done) {
        message = "Bitte zuerst Stille und/oder Musik messen";
        return false;
    }
    double noisePeak[4], musicAvg[4], musicPeak[4];
    for (int i = 0; i < 4; i++) {
        noisePeak[i] = calSilence.done ? calSilence.peak[i] : 0;
        musicAvg[i]  = calMusic.done ? calMusic.sum[i] / calMusic.frames : 0;
        musicPeak[i] = calMusic.done ? calMusic.peak[i] : 0;
    }

    if (calSilence.done) {
        // Threshold just above the loudest noise – but below the music average
        double *gates[3] = {&state.gateKick, &state.gateSnare, &state.gateHihat};
        for (int i = 0; i < 3; i++) {
            double g = noisePeak[i] * 1.3;
            if (calMusic.done && g > musicAvg[i] * 0.8) g = (noisePeak[i] + musicAvg[i]) / 2;
            *gates[i] = g;
        }
    }
    if (calMusic.done) {
        // "Loud" = peaks of the music (for light organ and level display)
        state.refKick  = max(musicPeak[0], state.gateKick * 2 + 1);
        state.refSnare = max(musicPeak[1], state.gateSnare * 2 + 1);
        state.refHihat = max(musicPeak[2], state.gateHihat * 2 + 1);
        state.refLevel = max(1.0, musicAvg[3] * 2);   // average ≈ half scale
    }
    state.calibrated = true;
    state.audioThreshold = 100;   // 100 % = calibrated threshold
    markConfigDirty();

    message = String("Eingemessen") + (calSilence.done ? " (Stille" : " (") +
              (calSilence.done && calMusic.done ? " + " : "") + (calMusic.done ? "Musik)" : ")");
    return true;
}

// ------------------------------------------------------------
// Spectrum for the web UI: 16 logarithmically spaced bands (~43 Hz … ~16 kHz)
// Each band has its own slowly rising noise floor → 0 during silence
// ------------------------------------------------------------
#define SPECTRUM_BANDS 16
static void updateSpectrum() {
    static int edges[SPECTRUM_BANDS + 1];
    static float floorDb[SPECTRUM_BANDS];
    static bool init = false;
    if (!init) {
        for (int b = 0; b <= SPECTRUM_BANDS; b++) {
            edges[b] = (int)roundf(powf(372.0f, (float)b / SPECTRUM_BANDS)); // bin 1 … 372
        }
        for (int b = 1; b <= SPECTRUM_BANDS; b++) if (edges[b] <= edges[b - 1]) edges[b] = edges[b - 1] + 1;
        for (int b = 0; b < SPECTRUM_BANDS; b++) floorDb[b] = 200;
        init = true;
    }
    for (int b = 0; b < SPECTRUM_BANDS; b++) {
        double sum = 0;
        int n = 0;
        for (int i = edges[b]; i < edges[b + 1] && i < SAMPLE_COUNT / 2; i++) { sum += vReal[i]; n++; }
        float db = 20.0f * log10f((float)(sum / max(1, n)) + 1.0f);
        // Noise floor: drops immediately, rises slowly (~1 dB/s)
        if (db < floorDb[b]) floorDb[b] = db;
        else floorDb[b] += 0.02f;
        float v = (db - floorDb[b] - 3.0f) / 40.0f;   // 40 dB display range
        state.spectrum[b] = (uint8_t)(constrain(v, 0.0f, 1.0f) * 255);
    }
}

void setupI2S() {
    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false
    };
    const i2s_pin_config_t pin_config = {
        .bck_io_num = 18, .ws_io_num = 5, .data_out_num = -1, .data_in_num = 19
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void taskAudio(void *pv) {
    setupI2S();
    size_t bytesRead = 0;
    int32_t raw[SAMPLE_COUNT];
    unsigned long dropStartTime = 0;

    for (;;) {
        i2s_read(I2S_NUM_0, raw, SAMPLE_COUNT * sizeof(int32_t), &bytesRead, portMAX_DELAY);
        uint32_t t0 = micros(); // Measure compute time from here (waiting for the microphone doesn't count)

        // Take over samples and determine the mean (DC offset of the microphone)
        double dc = 0;
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            vReal[i] = (float)(raw[i] >> 14); 
            vImag[i] = 0;
            dc += vReal[i];
        }
        dc /= SAMPLE_COUNT;

        // Remove DC offset: otherwise the Hamming window would "smear" it into
        // FFT bins 1-2 – exactly the kick band
        double currentLevel = 0;
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            vReal[i] -= dc;
            currentLevel += fabs(vReal[i]);
        }
        currentLevel /= SAMPLE_COUNT; // Level over all samples (for the UI display)
        state.level = currentLevel;

        // Note: there is deliberately no gain stage. All thresholds (noise gate,
        // light organ, drop detection) work on the raw energies; the former
        // auto-gain amplification was divided out again anyway.

        FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.compute(FFT_FORWARD);
        FFT.complexToMagnitude();

        // Compute raw frequency values (1 bin ≈ 43 Hz)
        double kRaw = bandEnergy(1, 2);      // ~43-86 Hz   kick
        double sRaw = bandEnergy(7, 15);     // ~300-650 Hz snare
        double hRaw = bandEnergy(180, 450);  // ~7.7-19 kHz hi-hat

        // --- IMPORTANT: write energy values into the global state ---
        // Only this way can effects.cpp (light organ) access them!
        state.kickEnergy  = kRaw;
        state.snareEnergy = sRaw;
        state.hihatEnergy = hRaw;

        // Calibration + spectrum
        double calValues[4] = {kRaw, sRaw, hRaw, currentLevel};
        calibrationFrame(calValues);
        updateSpectrum();

        // Beat detection with noise gate from the slider.
        // Calibrated: threshold = measured noise × slider % (100 = calibrated).
        // Otherwise as before (the old value is tiny compared to the energies).
        double gateScale = state.audioThreshold / 100.0;
        double gK = state.calibrated ? state.gateKick  * gateScale : state.audioThreshold;
        double gS = state.calibrated ? state.gateSnare * gateScale : state.audioThreshold * 0.6;
        double gH = state.calibrated ? state.gateHihat * gateScale : state.audioThreshold * 0.3;
        bool isKick  = kickDet.detect(kRaw, 1.25, 170, gK); 
        bool isSnare = snareDet.detect(sRaw, 1.3, 140, gS);
        if (isSnare) state.snareCount++;   // for the snare flash
        bool isHihat = hihatDet.detect(hRaw, 1.5, 70, gH);

        if (isKick) {
            state.audioKickCount++;          // real kicks from the music (for standby)
            state.lastAudioKickMs = millis();
        }

        unsigned long sinceLastKick = millis() - lastKickTime; // remember before updateKickBPM()
        if (isKick) {
            if (!state.bpmLocked && !state.syncReceiving) {   // as sync slave the beats come from the master
                state.lastBeatMs = millis();
                state.beatCount++;   // effect task evaluates the change
            }
            updateKickBPM();         // also records the kick for drop detection
        } else if (!state.bpmLocked && millis() - lastKickTime > 4000) {
            state.bpm = 0; 
        }

        // Locked BPM (tap tempo): beats come from an internal clock
        if (state.bpmLocked && state.bpm >= 30) {
            unsigned long now = millis();
            unsigned long interval = (unsigned long)(60000.0f / state.bpm);
            unsigned long sync = state.clockSyncMs;
            if (sync) {
                // Tap = beat: count immediately and align the clock from here
                state.clockSyncMs = 0;
                state.lastBeatMs = sync;
                state.beatCount++;
                nextClockBeat = sync + interval;
            }
            if ((long)(now - nextClockBeat) >= 0) {
                state.lastBeatMs = now;
                state.beatCount++;
                nextClockBeat += interval;
                if ((long)(now - nextClockBeat) >= 0) nextClockBeat = now + interval; // was away too long
            }
        }

        // Total energy (raw value, like kRaw/sRaw/hRaw)
        double totalE = 0;
        for(int i=2; i<250; i++) totalE += vReal[i];

        if (detectDrop(totalE, isKick, kRaw, sinceLastKick) && !state.syncReceiving) {
            state.drop = true;
            dropStartTime = millis();
            state.dropStartMs = dropStartTime;
            state.dropCount++;   // effect task starts the configured drop reaction
        }
        if (state.drop && (millis() - dropStartTime > DROP_HOLD_MS)) {
            state.drop = false;
        }

        recordLoad(loadStats.audioUs, loadStats.audioMaxUs, micros() - t0);
        vTaskDelay(1);
    }
}
