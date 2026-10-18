#pragma once
#include <ESPAsyncWebServer.h>

// Global web server objects (defined in web.cpp)
extern AsyncWebServer server;
extern AsyncWebSocket ws;

// Setup functions
void setupWiFi();
void setupWeb();

// Live status as JSON (for /api/status and the WebSocket)
String buildStatusJson(bool beat);

// Sends the live status via WebSocket to all browsers (call in loop()):
// immediately on beat/drop, otherwise every 100 ms
void handleLiveStatus();
