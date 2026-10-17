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

// ------------------------------------------------------------
// Drop-Erkennung
// Typischer Drop: Groove läuft → Breakdown ohne Kick → erster kräftiger Kick.
// Zweiter Weg: plötzlicher starker Energiesprung.
// ------------------------------------------------------------
#define DROP_BREAK_MIN_MS    1500   // So lange darf kein Kick kommen, damit es als Breakdown zählt
#define DROP_BREAK_MAX_MS   30000   // Längere Pausen = Musik war aus, kein Drop
#define DROP_MIN_GROOVE_KICKS   8   // So viele Kicks am Stück müssen vor dem Breakdown gelaufen sein
#define DROP_KICK_STRENGTH    0.7   // Kick nach dem Breakdown muss >= 70% eines normalen Kicks haben
#define DROP_ENERGY_RATIO     2.0   // Energiesprung kurz/lang für den zweiten Erkennungsweg
#define DROP_LOCKOUT_MS      8000   // Mindestabstand zwischen zwei Drops
#define DROP_HOLD_MS         1200   // So lange bleibt state.drop aktiv

unsigned long lastDrop = 0;
int    grooveKicks = 0;       // Kicks in Folge (Abstand < DROP_BREAK_MIN_MS)
int    grooveBeforeBreak = 0; // Groove-Länge vor dem aktuellen Breakdown
double kickAvg = 0;           // Durchschnittliche Stärke eines normalen Kicks
double energyShort = 0;       // Energie der letzten ~100 ms
double energyLong = 0;        // Energie der letzten ~2-3 s

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

// rawEnergy und kickEnergy sind Rohenergien ohne Verstärkung, damit der
// Lautstärkesprung des Drops erhalten bleibt.
// sinceLastKick = Zeit seit dem vorherigen Kick (vor diesem Frame)
bool detectDrop(double rawEnergy, bool kick, double kickEnergy, unsigned long sinceLastKick) {
    unsigned long now = millis();

    // Kurz- und Langzeit-Energie (ein Frame ≈ 23 ms)
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

    // Normale Kicks (nicht direkt nach einem Breakdown) lernen die Kick-Stärke
    if (!afterBreak) kickAvg = (kickAvg == 0) ? kickEnergy : kickAvg * 0.9 + kickEnergy * 0.1;

    if (afterBreak) {
        Serial.printf("Kick nach Breakdown: Pause %lums, Groove %d, Kick %.0f / Avg %.0f, Ratio %.2f\n",
                      sinceLastKick, grooveBeforeBreak, kickEnergy, kickAvg, ratio);
    }

    if (now - lastDrop < DROP_LOCKOUT_MS) return false;

    bool breakdownDrop = afterBreak && kickLearned && strongKick;
    bool energyDrop    = grooveKicks >= DROP_MIN_GROOVE_KICKS && strongKick && ratio > DROP_ENERGY_RATIO;

    if (breakdownDrop || energyDrop) {
        if (!state.dropEnabled) return false; // In der Web-UI abgeschaltet
        lastDrop = now;
        grooveBeforeBreak = 0;
        Serial.printf(">>> DROP DETECTED (%s) <<<\n", breakdownDrop ? "Breakdown" : "Energie");
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

        // Samples übernehmen und Mittelwert (DC-Offset des Mikrofons) bestimmen
        double dc = 0;
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            vReal[i] = (double)(raw[i] >> 14); 
            vImag[i] = 0;
            dc += vReal[i];
        }
        dc /= SAMPLE_COUNT;

        // DC-Offset entfernen: durch das Hamming-Fenster würde er sonst in die
        // FFT-Bins 1-2 "verschmieren" – genau das Kick-Band
        double currentLevel = 0;
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            vReal[i] -= dc;
            currentLevel += fabs(vReal[i]);
        }
        currentLevel /= SAMPLE_COUNT; // Pegel über alle Samples (für die UI-Anzeige)
        state.level = currentLevel;

        // Hinweis: Es gibt bewusst keine Gain-Stufe. Alle Schwellen (Noise-Gate,
        // Retro-Effekt, Drop-Erkennung) arbeiten auf den Rohenergien; die frühere
        // Auto-Gain-Verstärkung wurde ohnehin wieder herausgerechnet.

        FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT.compute(FFT_FORWARD);
        FFT.complexToMagnitude();

        // Frequenz-Rohwerte berechnen (1 Bin ≈ 43 Hz)
        double kRaw = bandEnergy(1, 2);      // ~43-86 Hz   Kick
        double sRaw = bandEnergy(7, 15);     // ~300-650 Hz Snare
        double hRaw = bandEnergy(180, 450);  // ~7.7-19 kHz Hi-Hat

        // --- WICHTIG: Energiewerte in den globalen State schreiben ---
        // Nur so kann die effects.cpp (Retro-Effekt) darauf zugreifen!
        state.kickEnergy  = kRaw;
        state.snareEnergy = sRaw;
        state.hihatEnergy = hRaw;

        // Beat-Detection mit Noise-Gate vom Slider
        bool isKick  = kickDet.detect(kRaw, 1.25, 170, (double)state.audioThreshold); 
        bool isSnare = snareDet.detect(sRaw, 1.3, 140, (double)state.audioThreshold * 0.6);
        bool isHihat = hihatDet.detect(hRaw, 1.5, 70, (double)state.audioThreshold * 0.3);

        unsigned long sinceLastKick = millis() - lastKickTime; // vor updateKickBPM() merken
        if (isKick) {
            state.lastBeatMs = millis();
            state.beatCount++;   // Effekt-Task wertet die Änderung aus
            updateKickBPM();
        } else if (millis() - lastKickTime > 4000) {
            state.bpm = 0; 
        }

        // Gesamtenergie (Rohwert, wie kRaw/sRaw/hRaw)
        double totalE = 0;
        for(int i=2; i<250; i++) totalE += vReal[i];

        if (detectDrop(totalE, isKick, kRaw, sinceLastKick)) {
            state.drop = true;
            dropStartTime = millis();
        }
        if (state.drop && (millis() - dropStartTime > DROP_HOLD_MS)) {
            state.drop = false;
        }

        vTaskDelay(1);
    }
}
