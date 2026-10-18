#include <Arduino.h>
#include <AsyncUDP.h>
#include <WiFi.h>
#include "netinput.h"
#include "state.h"

#define ARTNET_PORT 6454
#define SACN_PORT   5568

static AsyncUDP artnetUdp;
static AsyncUDP sacnUdp;

// Received DMX frame. The UDP callback runs in its own task,
// so copying is briefly locked.
static portMUX_TYPE netMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t netBuf[512];
static int netLen = 0;
static volatile unsigned long netLastMs = 0;

static void storeDmx(const uint8_t *data, int len) {
    len = constrain(len, 0, 512);
    portENTER_CRITICAL(&netMux);
    memcpy(netBuf, data, len);
    netLen = len;
    portEXIT_CRITICAL(&netMux);
    unsigned long now = millis();
    netLastMs = now ? now : 1;
}

bool netInputActive() {
    unsigned long last = netLastMs;
    return state.netInput && last && (millis() - last < NET_TIMEOUT_MS);
}

int copyNetDmx(uint8_t *dst) {
    portENTER_CRITICAL(&netMux);
    int len = netLen;
    memcpy(dst, netBuf, len);
    portEXIT_CRITICAL(&netMux);
    return len;
}

// ------------------------------------------------------------
// Art-Net
// ------------------------------------------------------------
static const uint8_t ARTNET_ID[8] = {'A', 'r', 't', '-', 'N', 'e', 't', 0};

// Reply to ArtPoll so PC software shows the controller in its device list
static void sendArtPollReply(AsyncUDPPacket &packet) {
    uint8_t r[239];
    memset(r, 0, sizeof(r));
    memcpy(r, ARTNET_ID, 8);
    r[8] = 0x00; r[9] = 0x21;                   // OpCode ArtPollReply (0x2100, little endian)

    IPAddress ip = (WiFi.getMode() & WIFI_MODE_STA) && WiFi.isConnected() ? WiFi.localIP() : WiFi.softAPIP();
    for (int i = 0; i < 4; i++) r[10 + i] = ip[i];
    r[14] = ARTNET_PORT & 0xFF; r[15] = ARTNET_PORT >> 8;

    int portAddr = state.netUniverse - 1;      // Art-Net counts from 0
    r[18] = (portAddr >> 8) & 0x7F;             // NetSwitch
    r[19] = (portAddr >> 4) & 0x0F;             // SubSwitch
    r[23] = 0xD0;                               // Status1: indicators normal, web configuration
    strncpy((char*)r + 26, "DMX-Controller", 17);                  // ShortName
    strncpy((char*)r + 44, "ESP32 DMX Audio Controller", 63);      // LongName
    strncpy((char*)r + 108, "#0001 [0000] OK", 63);                // NodeReport
    r[173] = 1;                                 // NumPorts (lo)
    r[174] = 0x80;                              // Port 1 can output Art-Net → DMX
    r[182] = netInputActive() ? 0x80 : 0x00;    // GoodOutput: data is being transmitted
    r[190] = portAddr & 0x0F;                   // SwOut port 1
    uint8_t mac[6];
    WiFi.macAddress(mac);
    memcpy(r + 201, mac, 6);
    for (int i = 0; i < 4; i++) r[207 + i] = ip[i]; // BindIp
    r[211] = 1;                                 // BindIndex
    r[212] = 0x08;                              // Status2: 15-bit port addresses

    artnetUdp.writeTo(r, sizeof(r), packet.remoteIP(), ARTNET_PORT);
}

static void onArtnetPacket(AsyncUDPPacket packet) {
    const uint8_t *d = packet.data();
    size_t len = packet.length();
    if (len < 10 || memcmp(d, ARTNET_ID, 8) != 0) return;

    uint16_t opcode = d[8] | (d[9] << 8);
    if (opcode == 0x2000) {                     // ArtPoll
        sendArtPollReply(packet);
        return;
    }
    if (opcode != 0x5000 || len < 18) return;   // ArtDmx only

    int portAddr = ((d[15] & 0x7F) << 8) | d[14]; // Net + SubUni
    if (portAddr != state.netUniverse - 1) return;
    int dmxLen = (d[16] << 8) | d[17];
    storeDmx(d + 18, min(dmxLen, (int)len - 18));
}

// ------------------------------------------------------------
// sACN / E1.31
// ------------------------------------------------------------
static const uint8_t ACN_ID[12] = {0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31, 0x37, 0x00, 0x00, 0x00};

static void onSacnPacket(AsyncUDPPacket packet) {
    const uint8_t *d = packet.data();
    size_t len = packet.length();
    if (len < 126 || memcmp(d + 4, ACN_ID, 12) != 0) return;
    if (d[21] != 0x04 || d[43] != 0x02 || d[117] != 0x02) return; // Root/framing/DMP vector
    if (d[125] != 0x00) return;                                   // normal DMX data only (start code 0)

    int universe = (d[113] << 8) | d[114];
    if (universe != state.netUniverse) return;
    int count = ((d[123] << 8) | d[124]) - 1;                     // without start code
    storeDmx(d + 126, min(count, (int)len - 126));
}

// ------------------------------------------------------------
void setupNetInput() {
    artnetUdp.close();
    sacnUdp.close();
    netLastMs = 0;
    if (!state.netInput) return;

    if (artnetUdp.listen(ARTNET_PORT)) artnetUdp.onPacket(onArtnetPacket);
    else Serial.println("Art-Net: Port konnte nicht geöffnet werden");

    // sACN is sent via multicast to 239.255.<universe hi>.<universe lo>
    IPAddress group(239, 255, (state.netUniverse >> 8) & 0xFF, state.netUniverse & 0xFF);
    if (sacnUdp.listenMulticast(group, SACN_PORT)) sacnUdp.onPacket(onSacnPacket);
    else Serial.println("sACN: Multicast konnte nicht geöffnet werden");

    Serial.printf("Netzwerk-Eingang aktiv: Universum %d (Art-Net %d, sACN %d)\n",
                  state.netUniverse, state.netUniverse - 1, state.netUniverse);
}
