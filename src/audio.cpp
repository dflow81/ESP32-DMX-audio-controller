#include <Arduino.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include "state.h"

// ------------------------------------------------------------
// Audio / FFT Setup
// ------------------------------------------------------------
#define SAMPLE_COUNT 1024
#define SAMPLE_RATE 44100

double vReal[SAMPLE_COUNT];
double vImag[SAMPLE_COUNT];
ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, SAMPLE_COUNT, SAMPLE_RATE);

// ------------------------------------------------------------
// Micro-Beat Detection Logik mit Noise-Gate
// ------------------------------------------------------------
#define HISTORY_SIZE 32 

struct BeatDetector {
    double history[HISTORY_SIZE];
    int index = 0;
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

        history[index] = energy;
        index = (index + 1) % HISTORY_SIZE;
        return isHit;
    }
};

BeatDetector kickDet, snareDet, hihatDet;

unsigned long lastKickTime = 0;
unsigned long lastHiHatTimes[20];
int hihatIndex = 0;
double energyHistory = 1.0;
unsigned long lastDrop = 0;

// ------------------------------------------------------------
// Hilfsfunktionen
// ------------------------------------------------------------

double bandEnergy(int startBin, int endBin) {
    double sum = 0;
    for (int i = startBin; i <= endBin; i++) sum += vReal[i];
    return sum;
}

void updateKickBPM() {
    unsigned long now = millis();
    if (lastKickTime > 0) {
        unsigned long diff = now - lastKickTime;
        if (diff > 270 && diff < 1333) { 
            float instantBPM = 60000.0f / diff;
            if (state.bpm < 10) state.bpm = instantBPM;
            else state.bpm = (state.bpm * 0.8f) + (instantBPM * 0.2f);
        }
    }
    lastKickTime = now;
}

bool detectDrop(double totalEnergy, bool kick, bool hihat) {
    unsigned long now = millis();
    if (hihat) {
        lastHiHatTimes[hihatIndex] = now;
        hihatIndex = (hihatIndex + 1) % 20;
    }
    int hihatRate = 0;
    for (int i = 0; i < 20; i++) {
        if (now - lastHiHatTimes[i] < 1200) hihatRate++; 
    }
    double ratio = totalEnergy / (energyHistory + 1.0);
    energyHistory = (energyHistory * 0.98) + (totalEnergy * 0.02); 

    if (now - lastDrop < 5000) return false;

    if (hihatRate > 7 && kick && ratio > 1.35) {
        lastDrop = now;
        Serial.println(">>> DROP DETECTED <<<");
        return true;
    }
    return false;
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
        .bck_io_num = 21, .ws_io_num = 5, .data_out_num = -1, .data_in_num = 26
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void taskAudio(void *pv) {
    setupI2S();
    size_t bytesRead = 0;
    int32_t raw[SAMPLE_COUNT];
    double targetLevel = 3500.0;
    unsigned long dropStartTime = 0;

    for (;;) {
        i2s_read(I2S_NUM_0, raw, SAMPLE_COUNT * sizeof(int32_t), &bytesRead, portMAX_DELAY);

        for (int i = 0; i < SAMPLE_COUNT; i++) {
            vReal[i] = (double)(raw[i] >> 14); 
            vImag[i] = 0;
        }

        double currentLevel = 0;
        for (int i = 0; i < 100; i++) currentLevel += abs(vReal[i]);
        currentLevel /= 100;
        
        double gain = state.autoGain ? (targetLevel / (currentLevel + 1.0)) : state.gain;
        gain = constrain(gain, 0.5, 8.0); 
        for (int i = 0; i < SAMPLE_COUNT; i++) vReal[i] *= gain;

        state.level = currentLevel;
        state.gain = gain;

        FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.compute(FFT_FORWARD);
        FFT.complexToMagnitude();

        // Frequenz-Rohwerte berechnen
        double kRaw = bandEnergy(1, 2) / (state.gain + 0.1);
        double sRaw = bandEnergy(7, 15) / (state.gain + 0.1);
        double hRaw = bandEnergy(180, 450) / (state.gain + 0.1);

        // --- WICHTIG: Energiewerte in den globalen State schreiben ---
        // Nur so kann die effects.cpp (Retro-Effekt) darauf zugreifen!
        state.kickEnergy  = kRaw;
        state.snareEnergy = sRaw;
        state.hihatEnergy = hRaw;

        // Beat-Detection mit Noise-Gate vom Slider
        bool isKick  = kickDet.detect(kRaw, 1.25, 170, (double)state.audioThreshold); 
        bool isSnare = snareDet.detect(sRaw, 1.3, 140, (double)state.audioThreshold * 0.6);
        bool isHihat = hihatDet.detect(hRaw, 1.5, 70, (double)state.audioThreshold * 0.3);

        state.beat = isKick;
        if (isKick) {
            updateKickBPM();
        } else if (millis() - lastKickTime > 4000) {
            state.bpm = 0; 
        }

        double totalE = 0;
        for(int i=2; i<250; i++) totalE += vReal[i];

        if (detectDrop(totalE, isKick, isHihat)) {
            state.drop = true;
            dropStartTime = millis();
        }
        if (state.drop && (millis() - dropStartTime > 1200)) {
            state.drop = false;
        }

        vTaskDelay(1);
    }
}
