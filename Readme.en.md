![ESP32 DMX Logo](doc/Logo.png)

[Deutsch](Readme.md) | **English**

[![ESP32 DMX Controller – video](https://img.youtube.com/vi/crHU-leWDkY/maxresdefault.jpg)](https://youtu.be/crHU-leWDkY?si=FmBRcN0zoc1HT1VW)

# 🎛️ ESP32 DMX Audio‑Reactive Controller

An audio‑reactive DMX controller based on an ESP32. It listens through an I2S microphone,
detects beats, tempo and drops, and uses them to drive RGB lamps, PARs, moving heads and a
fog machine. Everything is controlled from a web interface in the browser – on a phone,
tablet or PC, in German or English.

Great for parties, party rooms, small stages and installations.

---

## 📸 Screenshots

| Home | DMX configuration |
|---|---|
| ![Home](screenshots/Screenshot%202026-10-18%20at%2012-58-42%20ESP32%20DMX%20Controller.png) | ![DMX configuration](screenshots/Screenshot%202026-10-18%20at%2012-59-18%20DMX%20Konfiguration.png) |
| **Moving head** | **Chase editor** |
| ![Moving head](screenshots/Screenshot%202026-10-18%20at%2012-59-32%20MH%20Control%20Center.png) | ![Chase editor](screenshots/Screenshot%202026-10-18%20at%2012-59-45%20Lauflicht-Editor.png) |
| **DMX monitor** | **Wi‑Fi & network** |
| ![DMX monitor](screenshots/Screenshot%202026-10-18%20at%2013-00-01%20DMX%20Monitor.png) | ![Wi-Fi](screenshots/Screenshot%202026-10-18%20at%2012-59-02%20WLAN%20Konfiguration.png) |

The screenshots show the German interface; switch to English with **DE | EN** in the menu bar.

---

## 🚀 Features

### 🎵 Audio
- I2S microphone (e.g. INMP441), 1024‑point FFT
- Detection of **kick, snare and hi‑hat**, BPM from the music
- **Drop detection** (breakdown + strong kick or energy jump), can be turned off
- **Tap tempo** and "lock BPM" – an internal clock then replaces the detection
- **Microphone calibration** (silence + music) for clean noise thresholds in any room
- Live **spectrum** (16 bands), level and beat display in the web interface

### 💡 Effects & show
- **25 effects**, including fade, rainbow, chase, bounce, fire, aurora, strobe, disco,
  light organ and the audio effects **pump**, **snare flash** and **bass wave**
- **On beat**: effects run at ½, 1 or 2 beats per step
- **Smooth transitions** (adjustable, also per preset), **width** and **intensity** per effect
- **Color palette** with 2–8 custom colors for chase, disco, fade & co.
- **Groups** (up to 4) with their own effect and color
- **Presets** (up to 16) with effect, color, speed, master, groups and palette
- **Auto show**: cycle presets every N beats – optionally **matching the energy of the music**
- **Cue list** (up to 32 steps) for fixed sequences
- **Drop reaction**: strobe, white flash, color burst or preset – optionally with a fog burst
- **Custom chases** (4 slots, up to 8 lamps × 16 steps) in a graphical editor
- **Strobe**, **blackout** and **fog** as live buttons
- **Brightness follows volume**, **standby** without music, **schedule** (time from the internet)

### 🔦 Moving heads
- Joystick control, movement range via point A/B
- **BPM automation** with patterns: circle, figure eight, pan, tilt, random – offset (wave) or in sync
- Save and recall **positions** (up to 8)
- Extra channels **gobo, color wheel, prism, focus**

### 🖥️ Operation & setup
- Web interface with a live area and foldable settings, **German / English**
- **Virtual stage**: shows live what is sent to the lamps
- **DMX monitor** with **channel test** and "find lamp" (lamp flashes)
- **Templates** for typical fixture layouts (RGB, RGBW, PAR, spot, wash)
- Optional **password protection** for the whole interface
- **OLED display** (IP, BPM, effect, status) with burn‑in protection
- **Buttons / footswitch** for strobe, blackout, fog, tap and next preset

### 🌐 Network & integration
- Wi‑Fi with automatic hotspot and background reconnect
- **OTA updates** via the web interface or directly from PlatformIO
- **Art‑Net / sACN (E1.31)**: control from PC lighting software such as QLC+
- **MQTT / Home Assistant** with auto discovery (light, preset selector, BPM sensor)
- **Several controllers in sync** via ESP‑NOW (master/slave)
- **Backup & restore** of all settings as a file

---

## 🔌 Hardware

### Components
- ESP32 DevKit (ESP32‑WROOM)
- I2S microphone INMP441 (or ICS‑43434)
- RS485 transceiver MAX485 for the DMX output
- optional: 0.96" OLED 128×64 (SSD1306, I2C)
- optional: buttons / footswitch

Schematic, PCB layout and 3D views are in the [`PCB/`](PCB/) folder.

### Pinout
| Function | GPIO |
|---|---|
| I2S WS (LRCL) | 5 |
| I2S SCK (BCLK) | 18 |
| I2S SD (DOUT) | 19 |
| DMX TX → MAX485 DI | 32 |
| DMX enable → MAX485 DE/RE | 33 |
| OLED SDA | 22 |
| OLED SCL | 21 |
| Button strobe (hold) | 25 |
| Button blackout (on/off) | 26 |
| Button fog (hold) | 27 |
| Button tap tempo | 14 |
| Button next preset | 13 |

Connect each button between GPIO and GND (internal pull‑ups, no resistors needed).
The display is powered with 3.3 V.

---

## 🛠️ Installation

1. Open the project in **VS Code with PlatformIO**.
2. Connect the ESP32 via USB and run **Upload**. The file system (web interface) is uploaded
   automatically afterwards.
3. On first start the ESP32 opens the hotspot **`DMX-Controller`** (password `12345678`).
   Connect to it and open `http://192.168.4.1`.
4. Set up your network on the **WLAN** page. After that the controller is reachable at
   **`http://dmx-controller.local`** or the IP shown on the display.
5. Add your lamps under **DMX** (start address, type, optional group).
6. On the home page, **calibrate the microphone** under **Audio & drop**.

### Updates
- **Web interface:** **Update** page (`/update`), login `admin` / `dmxupdate`.
  Upload `.pio/build/esp32dev/firmware.bin` or `littlefs.bin`.
- **PlatformIO over Wi‑Fi:** select the `esp32dev_ota` environment and run **Upload**.
- Settings are stored in NVS and survive updates.

> **Security:** The default passwords are in [`include/ota.h`](include/ota.h)
> (update, with the matching `--auth=` in `platformio.ini`) and [`include/wifi.h`](include/wifi.h)
> (hotspot). Please change them – and keep in mind that they are readable by anyone in a public repo.
> A separate password for operation can be set on the **WLAN** page.
> **Forgot the password?** Hold the tap button (GPIO 14) while powering on.

---

## 📖 Operation at a glance

| Page | Contents |
|---|---|
| **Home** | Live status, effects, master, strobe/blackout/fog, presets; foldable: color, tempo, audio & drop, palette, groups, automation, cue list, virtual stage |
| **WLAN** | Network, password protection, synchronization, MQTT / Home Assistant |
| **DMX** | Fixtures and fixture types, templates, fog machine, schedule, display, buttons, Art‑Net/sACN |
| **Moving Head** | Joystick, point A/B, BPM automation, movement patterns, positions, gobo & co. |
| **Lauflicht** (chases) | Editor for custom chases |
| **Monitor** | Sent DMX values live, channel test, load of both cores |
| **Update** | Update firmware/file system, backup & restore |

### Art‑Net / sACN
Enable it on the **DMX** page and choose the universe. Universe 1 corresponds to
Art‑Net universe 0 or sACN universe 1 (as shown in QLC+). While data arrives, the PC controls
the lamps; 3 s after the last packet the effects take over again.

### MQTT / Home Assistant
Enter your broker on the **WLAN** page. Home Assistant finds the controller automatically as a
light (on/off = blackout, brightness = master, color, effects), a preset selector and a BPM sensor.

### Several controllers
On the **WLAN** page set one controller as **master** and the others as **slave**
(same group, same Wi‑Fi). The slaves follow beat, effect, colors and drops.

---

## 🧩 Project structure

| File | Purpose |
|---|---|
| `src/main.cpp` | Startup, effect task, auto show, cue list, standby, fog |
| `src/audio.cpp` | Microphone, FFT, beat/drop detection, calibration, spectrum |
| `src/effects.cpp` | All effects, palette, chases, drop reactions |
| `src/dmx.cpp` | DMX output (own task), transitions, moving head movement |
| `src/state.cpp` | State, saving/loading in NVS, presets, backup |
| `src/web*.cpp` | Web server, WebSocket and web API |
| `src/netinput.cpp` | Art‑Net / sACN |
| `src/sync.cpp` / `src/mqtt.cpp` | ESP‑NOW synchronization / MQTT & Home Assistant |
| `src/display.cpp`, `buttons.cpp`, `schedule.cpp`, `wifi.cpp`, `ota.cpp` | Display, buttons, schedule, Wi‑Fi, updates |
| `data/` | Web interface (HTML, CSS, JavaScript, translations) |

The work is split across both cores: **core 0** handles Wi‑Fi, audio analysis and the display,
**core 1** DMX output, the web server and the effects.

### Libraries
SparkFun DMX, arduinoFFT, ESPAsyncWebServer / AsyncTCP, ArduinoJson, Adafruit SSD1306, PubSubClient
