#include "web.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESP32HTTPUpdateServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "dmx.h"
#include "audio.h"
#include "presets.h"
#include "fade.h"

// ---------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------

WebServer server(80);
ESP32HTTPUpdateServer httpUpdater;

String cfg_sta_ssid = "DeinWLAN";
String cfg_sta_pass = "12345678";

// ---------------------------------------------------------
// SEND FILE FROM LITTLEFS
// ---------------------------------------------------------

void sendFile(const char* path, const char* mime) {
    if (!LittleFS.exists(path)) {
        server.send(404, "text/plain", "File not found");
        return;
    }
    File f = LittleFS.open(path, "r");
    server.streamFile(f, mime);
    f.close();
}

// ---------------------------------------------------------
// ROUTES: HTML PAGES
// ---------------------------------------------------------

void handleRoot() {
    sendFile("/index.html", "text/html");
}

void handleConfigPage() {
    sendFile("/config.html", "text/html");
}

// ---------------------------------------------------------
// API: STATUS (LEVEL, BEAT, BPM)
// ---------------------------------------------------------

void handleStatus() {
    StaticJsonDocument<256> doc;
    doc["level"] = level;
    doc["beat"] = beatDetected;
    doc["bpm"] = bpmEstimate;

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ---------------------------------------------------------
// API: CONFIG GET/POST
// ---------------------------------------------------------

void handleConfigAPI() {
    if (server.method() == HTTP_GET) {
        StaticJsonDocument<256> doc;
        doc["sta_ssid"] = cfg_sta_ssid;
        doc["sta_pass"] = cfg_sta_pass;

        doc["beatFadeEnabled"] = beatFadeEnabled;
        doc["beatFadeKick"] = beatFadeOnKick;
        doc["beatFadeSnare"] = beatFadeOnSnare;
        doc["beatFadeHiHat"] = beatFadeOnHiHat;
        doc["beatFadeStrength"] = beatFadeStrength;
        doc["beatFadeEvery"] = beatFadeEvery;

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
        return;
    }

    // POST
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "No body");
        return;
    }

    StaticJsonDocument<512> doc;
    deserializeJson(doc, server.arg("plain"));

    // UI slider config
    if (doc.containsKey("dimmer")) {
        masterDimmer = doc["dimmer"].as<int>() / 100.0f;
        gain = doc["gain"].as<int>() / 100.0f;
        autoGain = doc["autoGain"];
    }
    else {
        // Config page
        cfg_sta_ssid = (const char*)doc["sta_ssid"];
        cfg_sta_pass = (const char*)doc["sta_pass"];

        beatFadeEnabled = doc["beatFadeEnabled"];
        beatFadeOnKick = doc["beatFadeKick"];
        beatFadeOnSnare = doc["beatFadeSnare"];
        beatFadeOnHiHat = doc["beatFadeHiHat"];
        beatFadeStrength = doc["beatFadeStrength"];
        beatFadeEvery = doc["beatFadeEvery"];
    }

    server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------
// API: DMX MAP
// ---------------------------------------------------------

void handleDMXMap() {
    StaticJsonDocument<512> doc;
    JsonArray arr = doc.createNestedArray("lamps");

    for (int i = 0; i < 4; i++) {
        JsonObject o = arr.createNestedObject();
        o["index"] = i;
        o["base"] = lamps[i].baseChannel;

        o["dim"] = lamps[i].baseChannel + 0;
        o["r_ch"] = lamps[i].baseChannel + 1;
        o["g_ch"] = lamps[i].baseChannel + 2;
        o["b_ch"] = lamps[i].baseChannel + 3;
        o["strobe_ch"] = lamps[i].baseChannel + 4;

        o["r"] = lamps[i].r;
        o["g"] = lamps[i].g;
        o["b"] = lamps[i].b;
        o["strobe"] = lamps[i].strobe;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ---------------------------------------------------------
// API: PRESET LIST
// ---------------------------------------------------------

void handlePresetList() {
    StaticJsonDocument<512> doc;
    JsonArray arr = doc.createNestedArray("presets");

    for (int i = 0; i < presetCount; i++) {
        JsonObject o = arr.createNestedObject();
        o["id"] = i;
        o["name"] = presets[i].name;
        o["icon"] = presets[i].icon;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

// ---------------------------------------------------------
// API: PRESET SAVE
// ---------------------------------------------------------

void handlePresetSave() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "No body");
        return;
    }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, server.arg("plain"));

    String name = doc["name"] | "Preset";
    String icon = doc["icon"] | "🎛️";

    if (presetCount < 10) {
        presets[presetCount] = makeCurrentPreset(name);
        presets[presetCount].icon = icon;
        presetCount++;

        server.send(200, "application/json", "{\"ok\":true}");
    }
    else {
        server.send(400, "application/json", "{\"error\":\"full\"}");
    }
}

// ---------------------------------------------------------
// API: PRESET LOAD
// ---------------------------------------------------------

void handlePresetLoad() {
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "Missing id");
        return;
    }

    int id = server.arg("id").toInt();
    if (id < 0 || id >= presetCount) {
        server.send(400, "text/plain", "Invalid id");
        return;
    }

    applyPreset(presets[id]);
    activePreset = id;

    server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------
// API: PRESET DELETE
// ---------------------------------------------------------

void handlePresetDelete() {
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "Missing id");
        return;
    }

    int id = server.arg("id").toInt();
    if (id < 0 || id >= presetCount) {
        server.send(400, "text/plain", "Invalid id");
        return;
    }

    for (int i = id; i < presetCount - 1; i++)
        presets[i] = presets[i + 1];

    presetCount--;

    server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------
// API: PRESET EXPORT
// ---------------------------------------------------------

void handlePresetExport() {
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "Missing id");
        return;
    }

    int id = server.arg("id").toInt();
    if (id < 0 || id >= presetCount) {
        server.send(400, "text/plain", "Invalid id");
        return;
    }

    StaticJsonDocument<1024> doc;
    Preset &p = presets[id];

    doc["name"] = p.name;
    doc["icon"] = p.icon;
    doc["program"] = p.program;
    doc["paletteSize"] = p.paletteSize;
    doc["paletteRandom"] = p.paletteRandom;

    JsonArray pal = doc.createNestedArray("palette");
    for (int i = 0; i < 8; i++) {
        JsonObject c = pal.createNestedObject();
        c["r"] = p.palette[i].r;
        c["g"] = p.palette[i].g;
        c["b"] = p.palette[i].b;
    }

    JsonArray lampsArr = doc.createNestedArray("lampColorIndex");
    for (int i = 0; i < 4; i++)
        lampsArr.add(p.lampColorIndex[i]);

    doc["masterDimmer"] = p.masterDimmer;
    doc["gain"] = p.gain;
    doc["autoGain"] = p.autoGain;
    doc["strobeOnBeat"] = p.strobeOnBeat;
    doc["strobeModeEnabled"] = p.strobeModeEnabled;

    String out;
    serializeJsonPretty(doc, out);

    server.sendHeader("Content-Disposition", "attachment; filename=preset.json");
    server.send(200, "application/json", out);
}

// ---------------------------------------------------------
// API: PRESET IMPORT
// ---------------------------------------------------------

void handlePresetImport() {
    if (!server.hasArg("plain")) {
        server.send(400, "text/plain", "No JSON");
        return;
    }

    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "text/plain", "JSON error");
        return;
    }

    if (presetCount >= 10) {
        server.send(400, "text/plain", "Preset storage full");
        return;
    }

    Preset p;

    p.name = (const char*)doc["name"];
    p.icon = (const char*)doc["icon"];
    p.program = (ProgramMode)(int)doc["program"];
    p.paletteSize = doc["paletteSize"];
    p.paletteRandom = doc["paletteRandom"];

    JsonArray pal = doc["palette"];
    for (int i = 0; i < 8; i++) {
        p.palette[i].r = pal[i]["r"];
        p.palette[i].g = pal[i]["g"];
        p.palette[i].b = pal[i]["b"];
    }

    JsonArray lampsArr = doc["lampColorIndex"];
    for (int i = 0; i < 4; i++)
        p.lampColorIndex[i] = lampsArr[i];

    p.masterDimmer = doc["masterDimmer"];
    p.gain = doc["gain"];
    p.autoGain = doc["autoGain"];
    p.strobeOnBeat = doc["strobeOnBeat"];
    p.strobeModeEnabled = doc["strobeModeEnabled"];

    presets[presetCount++] = p;

    server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------
// API: PREVIEW (FADE)
// ---------------------------------------------------------

void handlePresetPreview() {
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "Missing id");
        return;
    }

    int id = server.arg("id").toInt();
    if (id < 0 || id >= presetCount) {
        server.send(400, "text/plain", "Invalid id");
        return;
    }

    Preset from = makeCurrentPreset("cur");
    Preset to = presets[id];

    fade_start_preview(from, to, 500);

    server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------
// WEB INIT
// ---------------------------------------------------------

void web_init() {
    LittleFS.begin(true);

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(cfg_sta_ssid.c_str(), cfg_sta_pass.c_str());
    WiFi.softAP("ESP32_DMX", "dmx12345");

    MDNS.begin("esp32-dmx");

    // HTML pages
    server.on("/", handleRoot);
    server.on("/config", handleConfigPage);

    // API
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/config", handleConfigAPI);
    server.on("/api/dmxmap", HTTP_GET, handleDMXMap);

    server.on("/api/presets", HTTP_GET, handlePresetList);
    server.on("/api/preset/save", HTTP_POST, handlePresetSave);
    server.on("/api/preset/load", HTTP_GET, handlePresetLoad);
    server.on("/api/preset/delete", HTTP_GET, handlePresetDelete);
    server.on("/api/preset/export", HTTP_GET, handlePresetExport);
    server.on("/api/preset/import", HTTP_POST, handlePresetImport);
    server.on("/api/preset/preview", HTTP_GET, handlePresetPreview);

    // OTA
    httpUpdater.setup(&server, "/update");

    server.begin();
    Serial.println("[WEB] Server gestartet");
}

// ---------------------------------------------------------
// WEB TASK
// ---------------------------------------------------------

void web_task(void *pvParameters) {
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
