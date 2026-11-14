// ASCOM Alpaca: management API, Telescope device 0 and UDP discovery.
// Shares the HTTP server owned by WebUI.
#pragma once
#include <Arduino.h>

namespace Alpaca {
void begin();          // registers the management routes and starts discovery
void poll();           // services UDP discovery
bool handleRequest();  // handles /api/v1/telescope/...; returns false if the URI is not Alpaca
}
