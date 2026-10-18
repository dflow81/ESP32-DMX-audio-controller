#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "sync.h"
#include "state.h"

#define SYNC_MAGIC   0x53584D44   // "DMXS"
#define SYNC_VERSION 1

#define FLAG_BEATSYNC  0x01
#define FLAG_BLACKOUT  0x02
#define FLAG_STROBE    0x04
#define FLAG_PALETTE   0x08

struct __attribute__((packed)) SyncPacket {
    uint32_t magic;
    uint8_t  version;
    uint8_t  group;
    uint32_t beatCount;
    float    bpm;
    uint16_t beatAgeMs;          // so lange ist der letzte Beat beim Senden her
    uint32_t dropCount;
    char     effect[16];
    uint8_t  r, g, b;
    uint8_t  master;
    uint16_t timerSpeed;
    float    stepsPerBeat;
    uint8_t  flags;
    uint8_t  paletteSize;
    uint8_t  palette[PALETTE_MAX][3];
};

static const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static bool espNowReady = false;
static wifi_interface_t peerIf = WIFI_IF_STA;

// Empfang (läuft im WLAN-Task) → Puffer; übernommen wird im Effekt-Task
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;
static SyncPacket rxPacket;
static volatile unsigned long rxMs = 0;

static void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    if (state.syncRole != SYNC_SLAVE || len != sizeof(SyncPacket)) return;
    const SyncPacket *p = (const SyncPacket *)data;
    if (p->magic != SYNC_MAGIC || p->version != SYNC_VERSION || p->group != state.syncGroup) return;
    portENTER_CRITICAL(&rxMux);
    memcpy(&rxPacket, p, sizeof(SyncPacket));
    portEXIT_CRITICAL(&rxMux);
    unsigned long now = millis();
    rxMs = now ? now : 1;
}

// Broadcast-Peer auf der gerade aktiven Schnittstelle (WLAN-Client oder Hotspot)
static void ensurePeer() {
    wifi_interface_t want = (WiFi.getMode() & WIFI_MODE_STA) ? WIFI_IF_STA : WIFI_IF_AP;
    if (esp_now_is_peer_exist(BROADCAST) && want == peerIf) return;
    if (esp_now_is_peer_exist(BROADCAST)) esp_now_del_peer(BROADCAST);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST, 6);
    peer.channel = 0;            // aktueller Kanal
    peer.ifidx = want;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) == ESP_OK) peerIf = want;
}

void setupSync() {
    // Kein WLAN-Stromsparmodus: sonst verpasst der Funk zwischendurch Sync-Pakete
    // (und die Web-UI reagiert träger)
    WiFi.setSleep(false);
    if (esp_now_init() != ESP_OK) {
        Serial.println("Sync: ESP-NOW konnte nicht gestartet werden");
        return;
    }
    esp_now_register_recv_cb(onReceive);
    espNowReady = true;
    ensurePeer();
}

void handleSync() {
    if (!espNowReady) return;

    // Slave: Verbindung zum Master noch frisch?
    unsigned long now = millis();
    unsigned long last = rxMs;
    state.syncReceiving = state.syncRole == SYNC_SLAVE && last && now - last < SYNC_TIMEOUT_MS;

    if (state.syncRole != SYNC_MASTER) return;

    // Master: sofort bei Beat/Drop/Blackout, sonst alle 50 ms
    static unsigned long lastSend = 0;
    static uint32_t lastBeat = 0, lastDrop = 0;
    static bool lastBlackout = false;
    bool event = state.beatCount != lastBeat || state.dropCount != lastDrop || state.blackout != lastBlackout;
    if (!event && now - lastSend < 50) return;

    SyncPacket p = {};
    p.magic = SYNC_MAGIC;
    p.version = SYNC_VERSION;
    p.group = state.syncGroup;
    p.beatCount = state.beatCount;
    p.bpm = state.bpm;
    p.beatAgeMs = min(65535UL, now - (unsigned long)state.lastBeatMs);
    p.dropCount = state.dropCount;
    {
        StateLock lock;
        strncpy(p.effect, state.currentEffect.c_str(), sizeof(p.effect) - 1);
        p.r = state.colorR; p.g = state.colorG; p.b = state.colorB;
        p.master = state.master;
        p.timerSpeed = state.timerSpeed;
        p.stepsPerBeat = state.stepsPerBeat;
        p.paletteSize = state.palette.size;
        memcpy(p.palette, state.palette.c, sizeof(p.palette));
        p.flags = (state.beatSync ? FLAG_BEATSYNC : 0) | (state.blackout ? FLAG_BLACKOUT : 0) |
                  ((long)(state.manualStrobeUntil - now) > 0 ? FLAG_STROBE : 0) |
                  (state.palette.on ? FLAG_PALETTE : 0);
    }

    ensurePeer();
    esp_now_send(BROADCAST, (const uint8_t *)&p, sizeof(p));
    lastSend = now;
    lastBeat = p.beatCount;
    lastDrop = p.dropCount;
    lastBlackout = state.blackout;
}

void applySyncFromMaster() {
    static uint32_t lastMasterBeat = 0, lastMasterDrop = 0;
    static bool first = true;
    if (!state.syncReceiving) { first = true; return; }
    SyncPacket p;
    portENTER_CRITICAL(&rxMux);
    memcpy(&p, &rxPacket, sizeof(p));
    portEXIT_CRITICAL(&rxMux);
    unsigned long now = millis();

    if (first) {   // beim (Wieder-)Verbinden keine alten Beats/Drops nachholen
        lastMasterBeat = p.beatCount;
        lastMasterDrop = p.dropCount;
        first = false;
    }

    // Beat und Tempo
    if (p.beatCount != lastMasterBeat) {
        lastMasterBeat = p.beatCount;
        state.lastBeatMs = now - p.beatAgeMs;
        state.beatCount++;
    }
    if (p.bpm > 0) state.bpm = p.bpm;

    // Drop (nur wenn hier die Drop-Erkennung an ist)
    if (p.dropCount != lastMasterDrop) {
        lastMasterDrop = p.dropCount;
        if (state.dropEnabled) {
            state.drop = true;
            state.dropStartMs = now;
            state.dropCount++;
        }
    }

    // Effekt, Farben, Einstellungen
    p.effect[sizeof(p.effect) - 1] = 0;
    String eff(p.effect);
    bool changed = eff != state.currentEffect || p.r != state.colorR || p.g != state.colorG || p.b != state.colorB;
    if (changed) {
        state.currentEffect = eff;
        state.colorR = p.r; state.colorG = p.g; state.colorB = p.b;
        state.fadeRequest = true;
        state.activePreset = -1;
    }
    state.master = p.master;
    state.timerSpeed = constrain((int)p.timerSpeed, TIMER_SPEED_MIN, TIMER_SPEED_MAX);
    state.stepsPerBeat = p.stepsPerBeat;
    state.beatSync = p.flags & FLAG_BEATSYNC;
    state.blackout = p.flags & FLAG_BLACKOUT;
    if (p.flags & FLAG_STROBE) state.manualStrobeUntil = now + 150;
    state.palette.on = p.flags & FLAG_PALETTE;
    state.palette.size = constrain((int)p.paletteSize, 2, PALETTE_MAX);
    memcpy(state.palette.c, p.palette, sizeof(state.palette.c));
}
