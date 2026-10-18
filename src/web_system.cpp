// System: WLAN, Backup/Wiederherstellung, Auslastung, DMX-Monitor
#include "web.h"
#include "web_util.h"
#include "state.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "ota.h"
#include "dmx.h"
#include "netinput.h"
#include "audio.h"
#include "schedule.h"
#include "mqtt.h"

void registerSystemRoutes() {
    // API: WLAN-Netzwerke scannen
    // Der Scan läuft im Hintergrund: 202 = läuft noch (UI fragt erneut), 200 = Liste
    server.on("/api/scanWifi", HTTP_GET, [](AsyncWebServerRequest *req){
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED) {
            // Im reinen AP-Modus kann nicht gescannt werden → AP+STA
            if (WiFi.getMode() == WIFI_AP) WiFi.mode(WIFI_AP_STA);
            WiFi.scanNetworks(true);
            req->send(202, "text/plain", "Scan gestartet");
            return;
        }
        if (n == WIFI_SCAN_RUNNING) {
            req->send(202, "text/plain", "Scan läuft");
            return;
        }

        // Doppelte SSIDs (mehrere Access Points) zusammenfassen, stärkstes Signal behalten
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.isEmpty()) continue;
            bool dup = false;
            for (JsonObject o : arr) {
                if (o["ssid"] == ssid) {
                    if (WiFi.RSSI(i) > o["rssi"].as<int>()) o["rssi"] = WiFi.RSSI(i);
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = ssid;
            o["rssi"] = WiFi.RSSI(i);
        }
        WiFi.scanDelete();

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: WLAN-Daten speichern → Neustart
    server.on("/api/saveWifi", HTTP_POST, [](AsyncWebServerRequest *req){}, NULL,
    [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
        const char *body = collectBody(req, data, len, index, total);
        if (!body) return;

        JsonDocument doc;
        if (deserializeJson(doc, body, total) || !doc["ssid"].is<const char*>()) {
            req->send(400, "text/plain", "JSON Error");
            return;
        }
        String ssid = doc["ssid"].as<String>();
        if (ssid.isEmpty() || ssid.length() > 32) {
            req->send(400, "text/plain", "Ungültige SSID");
            return;
        }
        {
            StateLock lock;
            state.wifi_ssid = ssid;
            state.wifi_pass = doc["pass"] | "";
            saveConfig();
        }
        req->send(200, "text/plain", "OK");
        scheduleRestart(1500);
    });

    // API: WLAN-Daten löschen → Neustart im Access-Point-Modus
    server.on("/api/clearWifi", HTTP_POST, [](AsyncWebServerRequest *req){
        {
            StateLock lock;
            state.wifi_ssid = "";
            state.wifi_pass = "";
            saveConfig();
        }
        req->send(200, "text/plain", "OK");
        scheduleRestart(1500);
    });

    // API: Auslastung der Tasks (zum Prüfen der Aufteilung auf die Cores)
    server.on("/api/load", HTTP_GET, [](AsyncWebServerRequest *req){
        static uint32_t lastFrames = 0;
        static unsigned long lastMs = 0;
        unsigned long now = millis();
        uint32_t frames = loadStats.dmxFrames;
        float fps = (lastMs && now > lastMs) ? (frames - lastFrames) * 1000.0f / (now - lastMs) : 0;
        lastFrames = frames;
        lastMs = now;

        JsonDocument doc;
        // Audio: Anteil der Rechenzeit an einem Mikrofon-Frame (1024 Samples = 23,2 ms)
        JsonObject a = doc["audio"].to<JsonObject>();
        a["core"] = CORE_AUDIO;
        a["us"] = loadStats.audioUs;
        a["maxUs"] = loadStats.audioMaxUs;
        a["percent"] = loadStats.audioUs * 100 / 23220;
        JsonObject e = doc["effect"].to<JsonObject>();
        e["core"] = CORE_LIGHT;
        e["us"] = loadStats.effectUs;
        e["maxUs"] = loadStats.effectMaxUs;
        JsonObject d = doc["dmx"].to<JsonObject>();
        d["core"] = CORE_LIGHT;
        d["us"] = loadStats.dmxUs;
        d["maxUs"] = loadStats.dmxMaxUs;
        d["fps"] = roundf(fps * 10) / 10;
        JsonObject disp = doc["display"].to<JsonObject>();
        disp["core"] = CORE_AUDIO;
        disp["us"] = loadStats.displayUs;

        // Freier Stack pro Task (Bytes) – zeigt, ob die Stack-Größen reichen
        JsonObject st = doc["stackFree"].to<JsonObject>();
        if (loadStats.audioTask)   st["audio"]   = uxTaskGetStackHighWaterMark(loadStats.audioTask);
        if (loadStats.effectTask)  st["effect"]  = uxTaskGetStackHighWaterMark(loadStats.effectTask);
        if (loadStats.dmxTask)     st["dmx"]     = uxTaskGetStackHighWaterMark(loadStats.dmxTask);
        if (loadStats.displayTask) st["display"] = uxTaskGetStackHighWaterMark(loadStats.displayTask);

        doc["heapFree"] = ESP.getFreeHeap();
        doc["heapMin"]  = ESP.getMinFreeHeap();
        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // API: Aktuell gesendete DMX-Werte (binär, 1 Byte pro Kanal) für den DMX-Monitor
    server.on("/api/dmx", HTTP_GET, [](AsyncWebServerRequest *req){
        static uint8_t buf[512];
        int n;
        {
            StateLock lock;
            n = getDMXOutput(buf, sizeof(buf));
        }
        AsyncWebServerResponse *res = req->beginResponse(200, "application/octet-stream", buf, n);
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
    });

    // API: Backup herunterladen (ohne WLAN-Passwort; Login wie bei /update)
    server.on("/api/backup", HTTP_GET, [](AsyncWebServerRequest *req){
        if (!adminAuthorized(req)) return;
        String json;
        {
            StateLock lock;
            json = exportConfigJson();
        }
        AsyncWebServerResponse *res = req->beginResponse(200, "application/json", json);
        res->addHeader("Content-Disposition", "attachment; filename=\"dmx-backup.json\"");
        req->send(res);
    });

    // API: Backup wiederherstellen → Neustart
    server.on("/api/restore", HTTP_POST,
        [](AsyncWebServerRequest *req){
            // Ohne Login wurde der Body ignoriert → Anmeldung anfordern
            if (!authActive() && !req->authenticate(OTA_USER, OTA_PASSWORD)) req->requestAuthentication();
        }, NULL,
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total){
            if (!authActive() && !req->authenticate(OTA_USER, OTA_PASSWORD)) return;
            const char *body = collectBody(req, data, len, index, total);
            if (!body) return;

            String error;
            bool ok;
            {
                StateLock lock;
                ok = importConfigJson(body, total, error);
            }
            if (!ok) {
                req->send(400, "text/plain", error);
                return;
            }
            req->send(200, "text/plain", "OK");
            scheduleRestart(1500);
        });
}
