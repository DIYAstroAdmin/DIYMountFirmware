// HTTP server: control/configuration page and its JSON API.
#pragma once
#include <Arduino.h>
#include <WebServer.h>

extern WebServer server;   // port 80, shared with the Alpaca API

namespace WebUI {
void begin();
void poll();
}
