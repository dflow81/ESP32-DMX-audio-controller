#include "web.h"
#include "state.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>

// ------------------------------------------------------------
// WebSocket
// ------------------------------------------------------------
AsyncWebSocket ws("/ws");
AsyncWebServer server(80);
// ------------------------------------------------------------
// HTML UI (eingebettet)
// ------------------------------------------------------------
static const char index_html[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>DMX Beat Controller</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:sans-serif;background:#111;color:#eee;margin:0;padding:10px;}
.card{background:#1c1c1c;border-radius:10px;padding:12px;margin-bottom:10px;}
label{display:block;margin:4px 0;}
input[type=range]{width:100%;}
.badge{display:inline-block;padding:3px 8px;border-radius:999px;background:#333;font-size:0.8rem;margin-right:4px;}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;margin-left:4px;}
.dot.on{background:#0f0;}
.dot.off{background:#555;}
.dmxbox{width:20px;height:20px;border-radius:4px;margin:2px;display:inline-block;border:1px solid #333;}
</style>
</head>
<body>
<h2>DMX Beat Controller</h2>

<div class="card">
  <h3>Mode</h3>
  <select id="mode">
    <option value="0">Solid</option>
    <option value="1">Beat Chase</option>
    <option value="2">Beat Pulse</option>
    <option value="3">Strobe</option>
  </select>
</div>

<div class="card">
  <h3>Master</h3>
  <label>Master Dimmer <span id="dval"></span></label>
  <input type="range" id="dimmer" min="0" max="255">
  <label>Gain <span id="gval"></span></label>
  <input type="range" id="gain" min="5" max="300">
  <label><input type="checkbox" id="autogain"> Auto Gain</label>
</div>

<div class="card">
  <h3>Strobe</h3>
  <label><input type="checkbox" id="strobeMode"> Strobe Modus</label>
  <label><input type="checkbox" id="strobeFast"> Nur bei schnellem Bass</label>
  <label>Strobe Level <span id="sval"></span></label>
  <input type="range" id="strobeLevel" min="0" max="255">
</div>

<div class="card">
  <h3>Beat / Pegel</h3>
  <div class="badge">Pegel: <span id="level">0</span></div>
  <div class="badge">BPM: <span id="bpm">0</span></div>
  <div class="badge">Beat <span class="dot off" id="beatDot"></span></div>
  <div class="badge">Drop <span class="dot off" id="dropDot"></span></div>
</div>

<div class="card">
  <h3>DMX Map</h3>
  <div id="dmxmap"></div>
</div>

<script>
let ws;

function connectWS(){
  ws = new WebSocket("ws://" + location.host + "/ws");

  ws.onmessage = ev => {
    let s = JSON.parse(ev.data);

    document.getElementById('mode').value = s.mode;
    document.getElementById('dimmer').value = s.masterDimmer;
    document.getElementById('gain').value = Math.round(s.gain*100);
    document.getElementById('autogain').checked = s.autoGain;
    document.getElementById('strobeMode').checked = s.strobeMode;
    document.getElementById('strobeFast').checked = s.strobeOnFastBass;
    document.getElementById('strobeLevel').value = s.strobeLevel;

    document.getElementById('dval').innerText = s.masterDimmer;
    document.getElementById('gval').innerText = s.gain.toFixed(2);
    document.getElementById('sval').innerText = s.strobeLevel;

    document.getElementById('level').innerText = s.level.toFixed(0);
    document.getElementById('bpm').innerText = s.bpm.toFixed(1);

    document.getElementById('beatDot').className = 'dot ' + (s.beat ? 'on':'off');
    document.getElementById('dropDot').className = 'dot ' + (s.drop ? 'on':'off');

    let map = document.getElementById('dmxmap');
    map.innerHTML = '';
    if (s.dmxPreview) {
      for (let i=0;i<s.dmxPreview.length;i+=4){
        let r = s.dmxPreview[i+1] || 0;
        let g = s.dmxPreview[i+2] || 0;
        let b = s.dmxPreview[i+3] || 0;
        let div = document.createElement('div');
        div.className = 'dmxbox';
        div.style.backgroundColor = `rgb(${r},${g},${b})`;
        map.appendChild(div);
      }
    }
  };

  ws.onclose = () => setTimeout(connectWS, 2000);
}

function sendState(){
  if(!ws || ws.readyState!==1) return;

  let msg = {
    mode: parseInt(document.getElementById('mode').value),
    masterDimmer: parseInt(document.getElementById('dimmer').value),
    gain: parseInt(document.getElementById('gain').value)/100.0,
    autoGain: document.getElementById('autogain').checked,
    strobeMode: document.getElementById('strobeMode').checked,
    strobeOnFastBass: document.getElementById('strobeFast').checked,
    strobeLevel: parseInt(document.getElementById('strobeLevel').value)
  };

  ws.send(JSON.stringify(msg));
}

window.addEventListener('load', ()=>{
  connectWS();

  ['mode','dimmer','gain','autogain','strobeMode','strobeFast','strobeLevel']
    .forEach(id=>{
      document.getElementById(id).addEventListener('input', ()=>{
        if(id==='dimmer') document.getElementById('dval').innerText = document.getElementById('dimmer').value;
        if(id==='gain') document.getElementById('gval').innerText = (parseInt(document.getElementById('gain').value)/100).toFixed(2);
        if(id==='strobeLevel') document.getElementById('sval').innerText = document.getElementById('strobeLevel').value;
        sendState();
      });
    });
});
</script>
</body>
</html>
)HTML";

// ------------------------------------------------------------
// JSON → WebSocket (State senden)
// ------------------------------------------------------------
String buildStateJson() {
    JsonDocument doc;

    doc["mode"] = (int)state.mode;
    doc["masterDimmer"] = state.masterDimmer;
    doc["strobeOnFastBass"] = state.strobeOnFastBass;
    doc["strobeMode"] = state.strobeMode;
    doc["strobeLevel"] = state.strobeLevel;

    doc["gain"] = state.gain;
    doc["autoGain"] = state.autoGain;

    doc["level"] = state.level;
    doc["bpm"] = state.bpm;
    doc["beat"] = state.beat;
    doc["drop"] = state.drop;

    JsonArray dmx = doc["dmxPreview"].to<JsonArray>();
    for (int i = 0; i < 32; i++)
        dmx.add(state.dmxPreview[i]);

    String out;
    serializeJson(doc, out);
    return out;
}

// ------------------------------------------------------------
// WebSocket → JSON (State empfangen)
// ------------------------------------------------------------
static void handleWsMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (!(info->final && info->opcode == WS_TEXT)) return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return;

    if (doc["mode"].is<int>())
        state.mode = (ControllerState::Mode)doc["mode"].as<int>();

    if (doc["masterDimmer"].is<int>())
        state.masterDimmer = doc["masterDimmer"].as<int>();

    if (doc["gain"].is<float>())
        state.gain = doc["gain"].as<float>();

    if (doc["autoGain"].is<bool>())
        state.autoGain = doc["autoGain"].as<bool>();

    if (doc["strobeMode"].is<bool>())
        state.strobeMode = doc["strobeMode"].as<bool>();

    if (doc["strobeOnFastBass"].is<bool>())
        state.strobeOnFastBass = doc["strobeOnFastBass"].as<bool>();

    if (doc["strobeLevel"].is<int>())
        state.strobeLevel = doc["strobeLevel"].as<int>();
}

// ------------------------------------------------------------
// WebSocket Events
// ------------------------------------------------------------
static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len)
{
    if (type == WS_EVT_CONNECT) {
        client->text(buildStateJson());
    }
    else if (type == WS_EVT_DATA) {
        handleWsMessage(arg, data, len);
    }
}

// ------------------------------------------------------------
// WiFi Setup
// ------------------------------------------------------------
void setupWiFi() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("DMX-Beats", "12345678");

    WiFi.begin("DEIN_SSID", "DEIN_PASS");

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        MDNS.begin("dmxbeat");
    }
}

// ------------------------------------------------------------
// Webserver Setup
// ------------------------------------------------------------
void setupWeb() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req){
        req->send(200, "text/html", index_html);
    });

    server.begin();
}
