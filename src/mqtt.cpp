#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "mqtt.h"
#include "state.h"

#define MQTT_RETRY_MS      10000   // Verbindungsversuch höchstens alle 10 s
#define MQTT_REFRESH_MS    30000   // Zustand spätestens alle 30 s neu senden

static WiFiClient net;
static PubSubClient mqtt(net);
static volatile bool reconfigure = false;
static String deviceId;            // z. B. "dmx_a1b2c3"

// Effekte für Home Assistant (wie in der Web-UI)
static const char *const EFFECTS[] = {
    "static", "fade", "rainbow", "chase", "bounce", "dualwave", "pulse", "sparkle", "strobe",
    "fire", "candle", "aurora", "ocean", "sunset", "fullstrobe", "randomstrobe",
    "dimmerwave", "dimmerchase", "dimmerbounce", "disco", "disco2", "lightorgan",
    "pump", "snareflash", "basswave"
};

static String topic(const char *suffix) { return state.mqttTopic + "/" + suffix; }

void mqttReconfigure() { reconfigure = true; }

// --- Home Assistant Discovery ---
static void addDevice(JsonDocument &doc) {
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = deviceId;
    dev["name"] = "DMX Controller";
    dev["manufacturer"] = "ESP32 DMX Audio Controller";
}

static String presetNames;   // zuletzt gemeldete Preset-Liste (bei Änderung neu melden)

static void publishDiscovery() {
    String json;

    // Licht
    {
        JsonDocument doc;
        doc["name"] = "Licht";
        doc["unique_id"] = deviceId + "_light";
        doc["schema"] = "json";
        doc["state_topic"] = topic("light");
        doc["command_topic"] = topic("light/set");
        doc["availability_topic"] = topic("status");
        doc["brightness"] = true;
        doc["supported_color_modes"][0] = "rgb";
        doc["effect"] = true;
        JsonArray list = doc["effect_list"].to<JsonArray>();
        for (const char *e : EFFECTS) list.add(e);
        {
            StateLock lock;
            for (int c = 0; c < USER_CHASES; c++)
                if (state.userChases[c].name.length()) list.add("user" + String(c + 1));
        }
        addDevice(doc);
        serializeJson(doc, json);
        mqtt.publish(("homeassistant/light/" + deviceId + "/config").c_str(), json.c_str(), true);
    }

    // Preset-Auswahl
    {
        JsonDocument doc;
        doc["name"] = "Preset";
        doc["unique_id"] = deviceId + "_preset";
        doc["state_topic"] = topic("preset");
        doc["command_topic"] = topic("preset/set");
        doc["availability_topic"] = topic("status");
        JsonArray opts = doc["options"].to<JsonArray>();
        opts.add("–");
        presetNames = "";
        {
            StateLock lock;
            for (auto &p : state.presets) { opts.add(p.name); presetNames += p.name + "\n"; }
        }
        addDevice(doc);
        json = "";
        serializeJson(doc, json);
        mqtt.publish(("homeassistant/select/" + deviceId + "_preset/config").c_str(), json.c_str(), true);
    }

    // BPM-Sensor
    {
        JsonDocument doc;
        doc["name"] = "BPM";
        doc["unique_id"] = deviceId + "_bpm";
        doc["state_topic"] = topic("bpm");
        doc["availability_topic"] = topic("status");
        doc["unit_of_measurement"] = "BPM";
        addDevice(doc);
        json = "";
        serializeJson(doc, json);
        mqtt.publish(("homeassistant/sensor/" + deviceId + "_bpm/config").c_str(), json.c_str(), true);
    }
}

// --- Zustand senden ---
static String lastLight, lastPreset;
static int lastBpm = -1;

static void publishState(bool force) {
    JsonDocument doc;
    String preset = "–";
    {
        StateLock lock;
        doc["state"] = state.blackout ? "OFF" : "ON";
        doc["brightness"] = state.master;
        doc["color_mode"] = "rgb";
        doc["color"]["r"] = state.colorR;
        doc["color"]["g"] = state.colorG;
        doc["color"]["b"] = state.colorB;
        doc["effect"] = state.currentEffect;
        int ap = state.activePreset;
        if (ap >= 0 && ap < (int)state.presets.size()) preset = state.presets[ap].name;
    }
    String light;
    serializeJson(doc, light);
    if (force || light != lastLight) { mqtt.publish(topic("light").c_str(), light.c_str(), true); lastLight = light; }
    if (force || preset != lastPreset) { mqtt.publish(topic("preset").c_str(), preset.c_str(), true); lastPreset = preset; }
    int bpm = (state.bpm < 10) ? 0 : (int)roundf(state.bpm);
    if (force || bpm != lastBpm) { mqtt.publish(topic("bpm").c_str(), String(bpm).c_str(), true); lastBpm = bpm; }
}

// --- Befehle aus Home Assistant ---
static void onMessage(char *t, byte *payload, unsigned int len) {
    String tp(t);
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];

    StateLock lock;
    if (tp == topic("light/set")) {
        JsonDocument doc;
        if (deserializeJson(doc, msg)) return;
        if (doc["state"].is<const char*>()) state.blackout = (doc["state"].as<String>() == "OFF");
        if (doc["brightness"].is<int>()) state.master = constrain((int)doc["brightness"], 0, 255);
        if (doc["color"].is<JsonObject>()) {
            state.colorR = doc["color"]["r"] | state.colorR;
            state.colorG = doc["color"]["g"] | state.colorG;
            state.colorB = doc["color"]["b"] | state.colorB;
            state.currentEffect = "static";   // wie in der Web-UI: Farbe → statisch
            state.activePreset = -1;
            state.fadeRequest = true;
        }
        if (doc["effect"].is<const char*>()) {
            state.currentEffect = doc["effect"].as<String>();
            state.activePreset = -1;
            state.fadeRequest = true;
        }
        markConfigDirty();
    } else if (tp == topic("preset/set")) {
        int idx = findPreset(msg);
        if (idx >= 0) applyPreset(idx, true);
    }
}

static void mqttTask(void *param) {
    unsigned long lastTry = 0, lastRefresh = 0;
    for (;;) {
        unsigned long now = millis();
        bool wanted = state.mqttOn && state.mqttHost.length() && WiFi.isConnected();

        if (reconfigure || !wanted) {
            reconfigure = false;
            if (mqtt.connected()) {
                mqtt.publish(topic("status").c_str(), "offline", true);
                mqtt.disconnect();
            }
            state.mqttConnected = false;
            lastTry = 0;
            if (!wanted) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        }

        if (!mqtt.connected()) {
            state.mqttConnected = false;
            if (lastTry == 0 || now - lastTry > MQTT_RETRY_MS) {
                lastTry = now;
                mqtt.setServer(state.mqttHost.c_str(), state.mqttPort);
                bool ok = state.mqttUser.length()
                    ? mqtt.connect(deviceId.c_str(), state.mqttUser.c_str(), state.mqttPass.c_str(),
                                   topic("status").c_str(), 0, true, "offline")
                    : mqtt.connect(deviceId.c_str(), topic("status").c_str(), 0, true, "offline");
                if (ok) {
                    Serial.println("MQTT verbunden");
                    mqtt.publish(topic("status").c_str(), "online", true);
                    mqtt.subscribe(topic("light/set").c_str());
                    mqtt.subscribe(topic("preset/set").c_str());
                    publishDiscovery();
                    publishState(true);
                    lastRefresh = now;
                    state.mqttConnected = true;
                } else {
                    Serial.printf("MQTT: Verbindung fehlgeschlagen (%d)\n", mqtt.state());
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        mqtt.loop();

        // Preset-Liste geändert → Home Assistant neu informieren
        String names;
        {
            StateLock lock;
            for (auto &p : state.presets) names += p.name + "\n";
        }
        if (names != presetNames) publishDiscovery();

        bool refresh = now - lastRefresh > MQTT_REFRESH_MS;
        publishState(refresh);
        if (refresh) lastRefresh = now;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setupMqtt() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char id[16];
    snprintf(id, sizeof(id), "dmx_%02x%02x%02x", mac[3], mac[4], mac[5]);
    deviceId = id;

    mqtt.setBufferSize(2048);   // Discovery mit Effekt-Liste ist größer als der Standard
    mqtt.setCallback(onMessage);
    mqtt.setSocketTimeout(2);
    xTaskCreatePinnedToCore(mqttTask, "mqttTask", 8192, NULL, 1, NULL, CORE_AUDIO);
}
