#include "audio.h"
#include <driver/i2s.h>
#include <esp_dsp.h>

// ---------------------------------------------------------
// I2S PIN CONFIGURATION
// ---------------------------------------------------------
#define I2S_WS   5
#define I2S_SD   26
#define I2S_SCK  21
#define I2S_PORT I2S_NUM_0

// ---------------------------------------------------------
// FFT SETTINGS
// ---------------------------------------------------------
const int FFT_SIZE = 512;

float fft_in[FFT_SIZE];
float fft_out[FFT_SIZE];
float windowFFT[FFT_SIZE];
float lastSpectrum[FFT_SIZE / 2];

// ---------------------------------------------------------
// GLOBAL AUDIO VARIABLES
// ---------------------------------------------------------
float level = 0.0f;
bool beatDetected = false;
float bpmEstimate = 0.0f;

bool kick = false;
bool snare = false;
bool hihat = false;

float spectralFlux = 0.0f;

// ---------------------------------------------------------
// BPM ESTIMATION
// ---------------------------------------------------------
static uint32_t beatTimes[32];
static int beatIndex = 0;
static bool lastBeatState = false;

static void updateBPM(bool beatNow) {
    uint32_t now = millis();

    if (beatNow && !lastBeatState) {
        beatTimes[beatIndex] = now;
        beatIndex = (beatIndex + 1) % 32;

        int count = 0;
        float sum = 0.0f;

        for (int i = 0; i < 31; i++) {
            uint32_t t1 = beatTimes[i];
            uint32_t t2 = beatTimes[(i + 1) % 32];

            if (!t1 || !t2 || t2 <= t1) continue;

            float dt = (t2 - t1) / 1000.0f;
            if (dt < 0.2f || dt > 2.0f) continue;

            sum += dt;
            count++;
        }

        if (count > 2) {
            float avg = sum / count;
            bpmEstimate = 60.0f / avg;
        }
    }

    lastBeatState = beatNow;
}

// ---------------------------------------------------------
// HELPER: ENERGY IN FREQUENCY BAND
// ---------------------------------------------------------
static float bandEnergy(float *fft, int startBin, int endBin) {
    float e = 0.0f;
    for (int i = startBin; i <= endBin; i++) {
        float re = fft[2 * i];
        float im = fft[2 * i + 1];
        e += re * re + im * im;
    }
    return e;
}

// ---------------------------------------------------------
// AUDIO INITIALIZATION
// ---------------------------------------------------------
void audio_init() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = -1,
        .data_in_num = I2S_SD
    };

    i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_PORT, &pin_config);

    // Prepare FFT window
    dsps_wind_hann_f32(windowFFT, FFT_SIZE);
    dsps_fft2r_init_fc32(NULL, FFT_SIZE);

    Serial.println("[AUDIO] I2S + FFT initialisiert");
}

// ---------------------------------------------------------
// AUDIO PROCESSING TASK
// ---------------------------------------------------------
void audio_task(void *pvParameters) {
    int32_t sampleBuffer[FFT_SIZE];
    size_t bytesRead = 0;

    static float avgKick = 0, avgSnare = 0, avgHat = 0;

    for (;;) {
        // Read audio samples
        i2s_read(I2S_PORT, (void*)sampleBuffer, sizeof(sampleBuffer), &bytesRead, portMAX_DELAY);
        int samples = bytesRead / sizeof(int32_t);
        if (samples < FFT_SIZE) continue;

        // ---------------------------------------------------------
        // LEVEL CALCULATION
        // ---------------------------------------------------------
        level = 0.0f;
        for (int i = 0; i < FFT_SIZE; i++) {
            float s = (float)sampleBuffer[i] / 2147483648.0f;
            fft_in[i] = s * windowFFT[i];
            level += fabsf(s);
        }
        level /= FFT_SIZE;

        // ---------------------------------------------------------
        // FFT
        // ---------------------------------------------------------
        dsps_fft2r_fc32(fft_in, FFT_SIZE);
        dsps_bit_rev_fc32(fft_in, FFT_SIZE);
        dsps_cplx2reC_fc32(fft_in, fft_out, FFT_SIZE);

        // ---------------------------------------------------------
        // SPECTRAL FLUX
        // ---------------------------------------------------------
        spectralFlux = 0.0f;
        for (int i = 0; i < FFT_SIZE / 2; i++) {
            float mag = sqrtf(fft_out[2 * i] * fft_out[2 * i] +
                              fft_out[2 * i + 1] * fft_out[2 * i + 1]);

            float diff = mag - lastSpectrum[i];
            if (diff > 0) spectralFlux += diff;

            lastSpectrum[i] = mag;
        }

        // Flux history for adaptive threshold
        static float fluxHistory[64];
        static int fluxIndex = 0;

        fluxHistory[fluxIndex] = spectralFlux;
        fluxIndex = (fluxIndex + 1) % 64;

        float mean = 0.0f;
        for (int i = 0; i < 64; i++) mean += fluxHistory[i];
        mean /= 64.0f;

        bool beatNow = (spectralFlux > mean * 1.5f);
        beatDetected = beatNow;

        // ---------------------------------------------------------
        // FREQUENCY BANDS FOR KICK / SNARE / HIHAT
        // ---------------------------------------------------------
        float sample_rate = 44100.0f;
        float f_bin = sample_rate / FFT_SIZE;

        auto freqToBin = [&](float f) {
            int b = (int)(f / f_bin);
            if (b < 1) b = 1;
            if (b > FFT_SIZE / 2 - 1) b = FFT_SIZE / 2 - 1;
            return b;
        };

        int kickStart  = freqToBin(40.0f);
        int kickEnd    = freqToBin(150.0f);

        int snareStart = freqToBin(150.0f);
        int snareEnd   = freqToBin(800.0f);

        int hatStart   = freqToBin(5000.0f);
        int hatEnd     = freqToBin(12000.0f);

        float eKick  = bandEnergy(fft_out, kickStart,  kickEnd);
        float eSnare = bandEnergy(fft_out, snareStart, snareEnd);
        float eHat   = bandEnergy(fft_out, hatStart,   hatEnd);

        // Adaptive smoothing
        const float alpha = 0.05f;
        avgKick  = (1 - alpha) * avgKick  + alpha * eKick;
        avgSnare = (1 - alpha) * avgSnare + alpha * eSnare;
        avgHat   = (1 - alpha) * avgHat   + alpha * eHat;

        kick  = (eKick  > avgKick  * 2.0f);
        snare = (eSnare > avgSnare * 2.0f);
        hihat = (eHat   > avgHat   * 2.0f);

        // ---------------------------------------------------------
        // BPM UPDATE
        // ---------------------------------------------------------
        updateBPM(beatNow);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
