#pragma once
#include <ESPAsyncWebServer.h>

extern AsyncWebSocket ws;

void setupWeb();
String buildStateJson();
