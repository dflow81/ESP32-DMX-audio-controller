#pragma once
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "state.h"

// Gemeinsame Helfer für die Web-Routen (definiert in web.cpp)

// Max. Größe eines Request-Bodys (Config, Cue-Liste, Lauflicht, Backup)
#define MAX_CONFIG_BODY 16384

// Config sofort speichern lassen (statt nach 5 s), erledigt der Effekt-Task
extern volatile bool configSaveNow;

// Sammelt einen Request-Body, der in mehreren Stücken ankommen kann.
// Gibt den kompletten Body zurück, sobald das letzte Stück da ist, sonst nullptr.
const char* collectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total);

void hexToRGB(String hex, uint8_t &r, uint8_t &g, uint8_t &b);
void presetToJson(JsonArray arr, int id, const Preset &p);

// Zugriff auf Update/Backup: mit Passwortschutz reicht dessen Login (prüft die Middleware),
// ohne Passwortschutz gilt der Update-Login aus ota.h. Fordert bei Bedarf die Anmeldung an.
bool adminAuthorized(AsyncWebServerRequest *req);

// Routen nach Themen
void registerShowRoutes();      // web_show.cpp
void registerFixtureRoutes();   // web_fixtures.cpp
void registerSystemRoutes();    // web_system.cpp
