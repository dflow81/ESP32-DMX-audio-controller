![ESP32 DMX Logo](doc/Logo.png)






<p align="center">
  <a href="https://www.youtube.com">
    <img src="https://www.youtube.com/watch?v=crHU-leWDkY&pp=ygUUZXNwMzIgZG14IGNvbnRyb2xsZXI%3D" alt="Video" width="400">
  </a>
</p>



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


