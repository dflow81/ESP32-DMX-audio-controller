![ESP32 DMX Logo](docs/Logo.png)


# 🎛️ ESP32 DMX Audio‑Reactive Controller

Ein leistungsstarker, modularer DMX‑Controller auf Basis eines ESP32.  
Er kombiniert:

- Echtzeit‑Audioanalyse (FFT, Kick/Snare/HiHat‑Erkennung)
- Beat‑Erkennung + BPM‑Tracking
- DMX‑Steuerung für mehrere RGB‑Lampen
- Moderne Web‑UI (ähnlich WLED)
- Presets, Preview‑Fades, Beat‑Fades
- OTA‑Updates
- LittleFS‑Dateisystem für HTML/CSS/JS

Ideal für Lichtsteuerung, Musikvisualisierung, Partys, Clubs oder Installationen.

---

## 🚀 Features

### 🎵 Audio‑Reaktiv
- I2S‑Audioeingang (z. B. INMP441)
- 512‑Punkte FFT (ESP‑DSP)
- Kick‑, Snare‑ und HiHat‑Erkennung
- BPM‑Berechnung über Beat‑Historie
- Spektralfluss‑Analyse

### 💡 DMX‑Steuerung
- 4 RGB‑Lampen (erweiterbar)
- Dimmer, RGB, Strobe
- 25 ms DMX‑Update‑Rate
- Master‑Dimmer

### 🎨 Programme
- **Solid Color**
- **Beat Color Cycle**
- **Beat Strobe**
- **Rainbow Slow**

### 🎚 Presets
- Speichern / Laden / Löschen
- Export / Import (JSON)
- Preview‑Fade beim Hover
- Beat‑Fade (Kick/Snare/HiHat)

### 🌐 Web‑UI (LittleFS)
- Moderne Oberfläche (HTML/CSS/JS)
- Live‑Status (Level, Beat, BPM)
- Preset‑Grid
- Konfigurationsseite
- OTA‑Update

---

## 📁 Projektstruktur

your-project/
│
├── platformio.ini
├── partitions.csv
├── prepare_fs.py
│
├── src/
│   ├── main.cpp
│   ├── dmx.cpp  / dmx.h
│   ├── audio.cpp  / audio.h
│   ├── web.cpp  / web.h
│   ├── presets.cpp  / presets.h
│   ├── fade.cpp  / fade.h
│
└── data/
├── index.html
├── config.html
├── style.css
└── script.js


---

## 🛠 Installation

### 1. Repository klonen
git clone <repo-url>

### 2. Abhängigkeiten installieren
PlatformIO erledigt das automatisch:

- esp_dmx  
- esp-dsp  
- ArduinoJson  
- ESP32HTTPUpdateServer  

### 3. LittleFS‑Dateien hochladen


### 4. Firmware flashen

---

## 🌐 Web‑Interface

Nach dem Start:

- AP: **ESP32_DMX**  
- Passwort: **dmx12345**  
- oder über WLAN‑Client‑Modus (falls konfiguriert)

Aufruf:
http://esp32-dmx.local

oder IP‑Adresse.

---

## 🔧 Konfiguration

Unter `/config` findest du:

- WLAN‑Einstellungen  
- Beat‑Fade‑Optionen  
- OTA‑Update  
- Systemparameter  

---

## 🎨 Presets

### Speichern
Über die UI → „💾 Speichern“

### Laden
Klick auf ein Preset im Grid

### Preview‑Fade
Hover über ein Preset → sanfter Übergang

### Export / Import
JSON‑Dateien über die API oder UI

---

## 🔌 Hardware

### Empfohlen:
- ESP32 DevKitC  
- INMP441 oder ICS‑43434 Mikrofon  
- DMX‑Transceiver (z. B. MAX485)  
- 4× RGB‑DMX‑Lampen (oder mehr)

### Pinbelegung (Standard)
| Funktion | Pin |
|---------|-----|
| I2S WS  | 5   |
| I2S SD  | 26  |
| I2S SCK | 21  |
| DMX TX  | 32  |

---

## 🧪 API‑Endpoints

| Endpoint | Beschreibung |
|----------|--------------|
| `/api/status` | Level, Beat, BPM |
| `/api/config` | GET/POST Konfiguration |
| `/api/dmxmap` | DMX‑Kanäle |
| `/api/presets` | Liste |
| `/api/preset/save` | Speichern |
| `/api/preset/load` | Laden |
| `/api/preset/delete` | Löschen |
| `/api/preset/export` | Export |
| `/api/preset/import` | Import |
| `/api/preset/preview` | Preview‑Fade |

---

## 🔄 OTA‑Update

Über die Web‑UI:

/update

Firmware hochladen → fertig.

---

## 📜 Lizenz

Dieses Projekt ist frei anpassbar und erweiterbar.

---

## ❤️ Credits

Erstellt mit:
- ESP32  
- PlatformIO  
- LittleFS  
- ESP‑DSP  
- ArduinoJson  
- viel Kaffee  

---

## 📬 Support

Wenn du Fragen hast oder Features möchtest, melde dich einfach.
