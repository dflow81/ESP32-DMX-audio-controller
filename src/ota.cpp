#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <LittleFS.h>
#include "ota.h"
#include "state.h"
#include "web.h"

// ------------------------------------------------------------
// Gemeinsame Logik für beide Update-Wege
// ------------------------------------------------------------

// Die Einstellungen liegen im NVS, ein Dateisystem-Update lässt sie also unberührt.
static void beginUpdate(bool fs) {
    StateLock lock;           // wartet, falls der Effekt-Task gerade speichert
    state.otaActive = true;   // ab jetzt kein Auto-Save mehr
    if (fs) LittleFS.end();   // Dateisystem aushängen, bevor es überschrieben wird
    Serial.printf("OTA: Update gestartet (%s)\n", fs ? "Dateisystem" : "Firmware");
}

// ------------------------------------------------------------
// Update-Seite (liegt in der Firmware, funktioniert auch ohne Dateisystem)
// ------------------------------------------------------------
static const char UPDATE_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Update - ESP32 DMX Controller</title>
<link rel="stylesheet" href="/style.css">
<style>
  body { background:#121212; color:#ddd; font-family:sans-serif; }
  .ota { max-width:520px; margin:20px auto; padding:0 16px; }
  .ota .card { background:#1e1e1e; border-radius:10px; padding:16px; margin-bottom:16px; }
  .ota input[type=file] { width:100%; margin:12px 0; color:#ccc; }
  .ota button { width:100%; padding:12px; }
  .ota .bar { height:10px; background:#333; border-radius:5px; overflow:hidden; margin-top:12px; }
  .ota .fill { height:100%; width:0; background:#4fc3f7; transition:width .2s; }
  .ota .hint { font-size:12px; color:#999; }
  #msg { margin-top:10px; min-height:1.2em; }
</style>
</head>
<body>
<div class="nav">
  <a href="/">Home</a>
  <a href="/wifi">WLAN</a>
  <a href="/dmxconfig">DMX</a>
  <a href="/mh">Moving Head</a>
  <a href="/update" class="active">Update</a>
</div>
<div class="ota">
  <div class="card">
    <b>Firmware</b>
    <div class="hint">Datei: .pio/build/esp32dev/firmware.bin</div>
    <input type="file" id="fw" accept=".bin">
    <button onclick="upload('fw','firmware')">Firmware hochladen</button>
  </div>
  <div class="card">
    <b>Dateisystem (Web-Oberfläche)</b>
    <div class="hint">Datei: .pio/build/esp32dev/littlefs.bin (erstellen mit "Build Filesystem Image").
      Deine Einstellungen (Fixtures, WLAN) bleiben erhalten.</div>
    <input type="file" id="fs" accept=".bin">
    <button onclick="upload('fs','fs')">Dateisystem hochladen</button>
  </div>
  <div class="card">
    <div class="bar"><div class="fill" id="fill"></div></div>
    <div id="msg"></div>
  </div>
</div>
<script>
function upload(inputId, type) {
  const file = document.getElementById(inputId).files[0];
  const msg = document.getElementById("msg");
  const fill = document.getElementById("fill");
  if (!file) { msg.textContent = "Bitte zuerst eine .bin-Datei wählen."; return; }
  document.querySelectorAll("button").forEach(b => b.disabled = true);
  const data = new FormData();
  data.append("update", file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/update?type=" + type);
  xhr.upload.onprogress = e => {
    if (e.lengthComputable) {
      const p = Math.round(e.loaded / e.total * 100);
      fill.style.width = p + "%";
      msg.textContent = "Hochladen … " + p + " %";
    }
  };
  xhr.onload = () => {
    if (xhr.status === 200) {
      msg.textContent = "Update erfolgreich. Neustart … die Seite lädt gleich neu.";
      setTimeout(() => location.href = "/", 10000);
    } else {
      msg.textContent = "Fehler: " + xhr.responseText;
      document.querySelectorAll("button").forEach(b => b.disabled = false);
    }
  };
  xhr.onerror = () => {
    msg.textContent = "Verbindung abgebrochen.";
    document.querySelectorAll("button").forEach(b => b.disabled = false);
  };
  xhr.send(data);
}
</script>
</body>
</html>)rawliteral";

// ------------------------------------------------------------
// Web-Update (/update)
// ------------------------------------------------------------
static bool webUploadDenied = false;
static String webUpdateError;

static void setupWebUpdate() {
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *req) {
        if (!req->authenticate(OTA_USER, OTA_PASSWORD)) return req->requestAuthentication();
        req->send(200, "text/html", UPDATE_PAGE);
    });

    server.on("/update", HTTP_POST,
        // Wird aufgerufen, wenn der Upload komplett ist
        [](AsyncWebServerRequest *req) {
            if (webUploadDenied) return req->requestAuthentication();
            bool ok = webUpdateError.isEmpty() && !Update.hasError();
            AsyncWebServerResponse *res = req->beginResponse(ok ? 200 : 500, "text/plain",
                                                             ok ? "OK" : webUpdateError.c_str());
            res->addHeader("Connection", "close");
            req->send(res);
            // Neustart erst nach dem Senden der Antwort (in loop())
            scheduleRestart(1000);
        },
        // Wird für jedes Stück der hochgeladenen Datei aufgerufen
        [](AsyncWebServerRequest *req, const String &filename, size_t index,
           uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                webUploadDenied = !req->authenticate(OTA_USER, OTA_PASSWORD);
                webUpdateError = "";
                if (webUploadDenied) return;

                bool fs = req->hasParam("type") && req->getParam("type")->value() == "fs";
                beginUpdate(fs);
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, fs ? U_SPIFFS : U_FLASH)) {
                    webUpdateError = String("Update.begin: ") + Update.errorString();
                }
            }
            if (webUploadDenied || !webUpdateError.isEmpty()) return;

            if (Update.write(data, len) != len) {
                webUpdateError = String("Schreiben: ") + Update.errorString();
                Update.abort();
                return;
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("OTA: Web-Update fertig (%u Bytes)\n", (unsigned)(index + len));
                } else {
                    webUpdateError = String("Abschluss: ") + Update.errorString();
                }
            }
        });
}

// ------------------------------------------------------------
// ArduinoOTA (Upload direkt aus PlatformIO per WLAN)
// ------------------------------------------------------------
static void setupArduinoOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
        beginUpdate(ArduinoOTA.getCommand() == U_SPIFFS);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("OTA: fertig, Neustart");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("OTA: Fehler %u\n", error);
        scheduleRestart(1000); // sauber neu starten (Dateisystem war evtl. ausgehängt)
    });
    ArduinoOTA.begin(); // startet auch mDNS → http://dmx-controller.local
}

void setupOTA() {
    setupWebUpdate();
    setupArduinoOTA();
    Serial.printf("OTA bereit: http://%s.local/update\n", OTA_HOSTNAME);
}

void handleOTA() {
    ArduinoOTA.handle();
}
