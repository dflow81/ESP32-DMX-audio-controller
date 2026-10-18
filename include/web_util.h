#pragma once
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "state.h"

// Shared helpers for the web routes (defined in web.cpp)

// Max. size of a request body (config, cue list, chase, backup)
#define MAX_CONFIG_BODY 16384

// Have the config saved immediately (instead of after 5 s), done by the effect task
extern volatile bool configSaveNow;

// Collects a request body that may arrive in several chunks.
// Returns the complete body once the last chunk is there, otherwise nullptr.
const char* collectBody(AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total);

void hexToRGB(String hex, uint8_t &r, uint8_t &g, uint8_t &b);
void presetToJson(JsonArray arr, int id, const Preset &p);

// Access to update/backup: with password protection its login suffices (checked by the middleware),
// without password protection the update login from ota.h applies. Requests authentication if needed.
bool adminAuthorized(AsyncWebServerRequest *req);

// Routes by topic
void registerShowRoutes();      // web_show.cpp
void registerFixtureRoutes();   // web_fixtures.cpp
void registerSystemRoutes();    // web_system.cpp
