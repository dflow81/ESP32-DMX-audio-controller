#include <Arduino.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include "state.h"

// ------------------------------------------------------------
// Audio / FFT
// ------------------------------------------------------------
#define SAMPLE_COUNT 1024
#define SAMPLE_RATE 44100

double vReal[SAMPLE_COUNT];
double vImag[SAMPLE_COUNT];
ArduinoFFT<double> FFT(vReal, vImag, SAMPLE_COUNT, SAMPLE_RATE);

// ------------------------------------------------------------
// Beat Detection State
// ------------------------------------------------------------
unsigned long lastKick = 0;
unsigned long lastSnare = 0;
unsigned long lastHiHat = 0;

double prevKick[10];
double prevSnare[10];
double prevHiHat[300];

float kickBPM = 0;
unsigned long lastKickTime = 0;

// Wird von dmx.cpp genutzt
int currentColorStep = 0;
unsigned long dropStrobeUntil = 0;

// ------------------------------------------------------------
// Drop Detection
// ------------------------------------------------------------
unsigned long lastHiHatTimes[20];
int hihatIndex = 0;
double prevTotalEnergy = 1;
unsigned long lastDrop = 0;

// ------------------------------------------------------------
// Autogain
// ------------------------------------------------------------
double targetLevel = 5000.0;
double gainMin = 0.2;
double gainMax = 10.0;

// ------------------------------------------------------------
// Helper: Band Energy
// ------------------------------------------------------------
double bandEnergy(int startBin, int endBin) {
    double sum = 0;
    for (int i = startBin; i <= endBin; i++) sum += vReal[i];
    return sum;
}

// ------------------------------------------------------------
// Helper: Flux
// ------------------------------------------------------------
double bandFlux(double* prev, int startBin, int endBin) {
    double flux = 0;
    for (int i = startBin; i <= endBin; i++) {
        double diff = vReal[i] - prev[i];
        if (diff > 0) flux += diff;
        prev[i] = vReal[i];
    }
    return flux;
}

// ------------------------------------------------------------
// Generic Event Detector
// ------------------------------------------------------------
bool detectEvent(double energy, double flux, double &avgEnergy, double &avgFlux,
                 unsigned long &lastTime, int minGap, double eMul, double fMul)
{
    avgEnergy = avgEnergy * 0.98 + energy * 0.02;
    avgFlux   = avgFlux   * 0.90 + flux   * 0.10;

    if (energy < avgEnergy * 0.5) return false;

    bool ePeak = energy > avgEnergy * eMul;
    bool fPeak = flux   > avgFlux   * fMul;

    unsigned long now = millis();

    if (ePeak && fPeak && (now - lastTime) > minGap) {
        lastTime = now;
        return true;
    }

    return false;
}

// ------------------------------------------------------------
// BPM Tracking
// ------------------------------------------------------------
void updateKickBPM() {
    unsigned long now = millis();
    if (lastKickTime > 0) {
        float interval = (now - lastKickTime) / 60000.0f;
        if (interval > 0.2 && interval < 1.0) {
            kickBPM = 1.0f / interval;
        }
    }
    lastKickTime = now;
}

// ------------------------------------------------------------
// Drop Detection
// ------------------------------------------------------------
bool detectDrop(double bassEnergy, bool kick, bool hihat) {
    unsigned long now = millis();

    if (hihat) {
        lastHiHatTimes[hihatIndex] = now;
        hihatIndex = (hihatIndex + 1) % 20;
    }

    int hihatRate = 0;
    for (int i = 0; i < 20; i++)
        if (now - lastHiHatTimes[i] < 1000) hihatRate++;

    double totalEnergy = 0;
    for (int i = 1; i < 200; i++) totalEnergy += vReal[i];

    double energyJump = totalEnergy / (prevTotalEnergy + 1);
    prevTotalEnergy = totalEnergy;

    bool buildUp = hihatRate > 8;
    bool energyExplosion = energyJump > 1.8;

    if (now - lastDrop < 1500) return false;

    if (buildUp && kick && energyExplosion) {
        lastDrop = now;
        return true;
    }

    return false;
}

// ------------------------------------------------------------
// I2S Setup (INMP441)
// ------------------------------------------------------------
void setupI2S() {

    const i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   // INMP441 = LEFT
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,  // WICHTIG!
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    const i2s_pin_config_t pin_config = {
        .bck_io_num = 21,   // SCK
        .ws_io_num = 5,     // L/R
        .data_out_num = -1,
        .data_in_num = 26   // SD
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    // INMP441 braucht dieses Timing
    i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_32BIT, I2S_CHANNEL_MONO);
}

// ------------------------------------------------------------
// Audio Task
// ------------------------------------------------------------
void taskAudio(void *pv) {
    setupI2S();

    size_t bytesRead = 0;
    int32_t raw[SAMPLE_COUNT];

    static double avgKickE = 0, avgKickF = 0;
    static double avgSnareE = 0, avgSnareF = 0;
    static double avgHiHatE = 0, avgHiHatF = 0;

    for (;;) {

        // --- Read I2S ---
        i2s_read(I2S_NUM_0, raw, SAMPLE_COUNT * sizeof(int32_t), &bytesRead, portMAX_DELAY);

        for (int i = 0; i < SAMPLE_COUNT; i++) {
            vReal[i] = (double)(raw[i] >> 14);  // INMP441 liefert 24‑Bit in 32‑Bit Frame
            vImag[i] = 0;
        }

        // --- Autogain ---
        double sum = 0;
        for (int i = 0; i < SAMPLE_COUNT; i++) sum += abs(vReal[i]);
        double currentLevel = sum / SAMPLE_COUNT;
        if (currentLevel < 1) currentLevel = 1;

        double gain = state.autoGain ? (targetLevel / currentLevel) : state.gain;
        gain = constrain(gain, gainMin, gainMax);

        for (int i = 0; i < SAMPLE_COUNT; i++) vReal[i] *= gain;

        state.level = currentLevel;
        state.gain = gain;

        // --- FFT ---
        FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.compute(FFT_FORWARD);
        FFT.complexToMagnitude();

        // --- Bands ---
        double kickEnergy  = bandEnergy(1, 3);
        double kickFlux    = bandFlux(prevKick, 1, 3);

        double snareEnergy = bandEnergy(4, 6);
        double snareFlux   = bandFlux(prevSnare, 4, 6);

        double hihatEnergy = bandEnergy(140, 280);
        double hihatFlux   = bandFlux(prevHiHat, 140, 280);

        // --- Detect ---
        //bool kick = detectEvent(kickEnergy, kickFlux, avgKickE, avgKickF, lastKick, 120, 1.4, 1.8);
        
        bool kick = detectEvent(kickEnergy, kickFlux, avgKickE, avgKickF, lastKick, 100, 1.1, 1.2);
        bool snare = detectEvent(snareEnergy, snareFlux, avgSnareE, avgSnareF, lastSnare, 120, 1.5, 1.7);
        bool hihat = detectEvent(hihatEnergy, hihatFlux, avgHiHatE, avgHiHatF, lastHiHat, 40, 1.3, 1.5);

        (void)snare;

        state.beat = kick;

        if (kick) updateKickBPM();
        state.bpm = kickBPM;

        bool drop = detectDrop(kickEnergy, kick, hihat);
        state.drop = drop;

        if (kick && state.mode == ControllerState::MODE_BEAT_CHASE)
            currentColorStep = (currentColorStep + 1) % 4;

        if (drop && state.strobeOnFastBass)
            dropStrobeUntil = millis() + 200;

        vTaskDelay(1);
    }
}
