#pragma once
#include <ESPAsyncWebServer.h>

// Globale Webserver-Objekte (definiert in web.cpp)
extern AsyncWebServer server;
//extern AsyncWebSocket ws;

// Setup-Funktionen
void setupWiFi();
void setupWeb();

// JSON für WebSocket
String buildStateJson();
