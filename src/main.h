#include <Arduino.h>
#include "dmx.h"
#include "audio.h"
#include "web.h"
#include "presets.h"
#include "fade.h"

TaskHandle_t dmxTaskHandle;
TaskHandle_t audioTaskHandle;
TaskHandle_t webTaskHandle;

// ---------------------------------------------------------
// PROGRAM LOGIC (DMX OUTPUT BASED ON CURRENT PROGRAM)
// ---------------------------------------------------------

void apply_programs() {
  // If a fade (preview or beat-fade) is active, it overrides everything
  if (previewFade.active) {
    fade_process();
    return;
  }

  switch (currentProgram) {

    case PROGRAM_SOLID: {
      for (int i = 0; i < 4; i++) {
        RGB c = palette[lampColorIndex[i] % paletteSize];
        lamps[i].r = c.r;
        lamps[i].g = c.g;
        lamps[i].b = c.b;
        lamps[i].strobe = 0;
      }
      break;
    }

    case PROGRAM_BEAT_COLOR_CYCLE: {
      static uint8_t baseIndex = 0;
      if (beatDetected) baseIndex++;

      for (int i = 0; i < 4; i++) {
        RGB c = palette[(baseIndex + i) % paletteSize];
        lamps[i].r = c.r;
        lamps[i].g = c.g;
        lamps[i].b = c.b;
        lamps[i].strobe = 0;
      }
      break;
    }

    case PROGRAM_BEAT_STROBE: {
      RGB c = palette[0];
      for (int i = 0; i < 4; i++) {
        lamps[i].r = c.r;
        lamps[i].g = c.g;
        lamps[i].b = c.b;
        lamps[i].strobe = (beatDetected && strobeModeEnabled) ? 255 : 0;
      }
      break;
    }

    case PROGRAM_RAINBOW_SLOW: {
      static uint16_t t = 0;
      t++;

      for (int i = 0; i < 4; i++) {
        float phase = (t + i * 50) / 200.0f;

        float r = (sinf(phase) + 1.0f) * 127.5f;
        float g = (sinf(phase + 2.094f) + 1.0f) * 127.5f;
        float b = (sinf(phase + 4.188f) + 1.0f) * 127.5f;

        lamps[i].r = (uint8_t)r;
        lamps[i].g = (uint8_t)g;
        lamps[i].b = (uint8_t)b;
        lamps[i].strobe = 0;
      }
      break;
    }
  }

  // ---------------------------------------------------------
  // BEAT-FADE TRIGGER
  // ---------------------------------------------------------
  if (fade_should_trigger_beat()) {
    Preset cur = makeCurrentPreset("cur");
    Preset target = cur;

    // Example: shift palette index on beat
    for (int i = 0; i < 4; i++) {
      target.lampColorIndex[i] = (target.lampColorIndex[i] + 1) % paletteSize;
    }

    fade_start_beat(cur, target);
  }
}

// ---------------------------------------------------------
// DMX MAIN TASK
// ---------------------------------------------------------

void dmx_main_task(void *pvParameters) {
  for (;;) {
    apply_programs();
    dmx_update_buffer();
    dmx_write(dmxPort, dmxData, DMX_UNIVERSE_SIZE);
    dmx_send(dmxPort, DMX_UNIVERSE_SIZE);
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

// ---------------------------------------------------------
// SETUP
// ---------------------------------------------------------

void setup() {
  Serial.begin(115200);

  dmx_init();
  audio_init();
  presets_init_defaults();
  web_init();

  xTaskCreatePinnedToCore(dmx_main_task, "DMX", 4096, NULL, 2, &dmxTaskHandle, 1);
  xTaskCreatePinnedToCore(audio_task, "Audio", 4096, NULL, 3, &audioTaskHandle, 0);
  xTaskCreatePinnedToCore(web_task, "Web", 4096, NULL, 1, &webTaskHandle, 1);
}

// ---------------------------------------------------------
// LOOP (unused)
// ---------------------------------------------------------

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
