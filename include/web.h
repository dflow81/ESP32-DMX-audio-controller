#pragma once
#include <ESPAsyncWebServer.h>

// Globale Webserver-Objekte (definiert in web.cpp)
extern AsyncWebServer server;
extern AsyncWebSocket ws;

// Setup-Funktionen
void setupWiFi();
void setupWeb();

// Live-Status als JSON (für /api/status und den WebSocket)
String buildStatusJson(bool beat);

// Schickt den Live-Status per WebSocket an alle Browser (in loop() aufrufen):
// sofort bei Beat/Drop, sonst alle 100 ms
void handleLiveStatus();
