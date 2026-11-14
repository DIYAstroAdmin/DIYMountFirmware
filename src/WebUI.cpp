#include "WebUI.h"
#include <WiFi.h>
#include "Alpaca.h"
#include "Astro.h"
#include "Config.h"
#include "Mount.h"
#include "Settings.h"
#include "WebPage.h"

WebServer server(80);

namespace WebUI {

static const double SIDEREAL_DEG_S = 360.0 / 86164.0905;
static const uint32_t MOVE_TIMEOUT_MS = 1500;   // held moves stop unless refreshed by the page
static bool rebootRequested = false;
static uint32_t rebootAt = 0;

static const char *boolText(bool b) { return b ? "true" : "false"; }

static void sendJson(const String &body) { server.send(200, "application/json", body); }

static void sendResult(MountError e) {
  if (e == M_OK) sendJson("{\"ok\":true}");
  else sendJson(String("{\"ok\":false,\"error\":\"") + Mount::errorText(e) + "\"}");
}

static void sendFail(const char *msg) { sendJson(String("{\"ok\":false,\"error\":\"") + msg + "\"}"); }

static void handleStatus() {
  MountStatus st;
  mount.getStatus(st);
  const char *state = "idle";
  if (st.state == MountState::Homing) state = "homing";
  else if (st.parking) state = "parking";
  else if (st.state == MountState::Slewing) state = "slewing";
  else if (st.atPark) state = "parked";
  else if (st.tracking) state = "tracking";

  String j = "{";
  j += "\"ra\":" + String(st.ra, 6);
  j += ",\"dec\":" + String(st.dec, 5);
  j += ",\"ha\":" + String(st.ha / 15.0, 6);
  j += ",\"lst\":" + String(st.lst, 6);
  j += ",\"alt\":" + String(st.alt, 3);
  j += ",\"az\":" + String(st.az, 3);
  j += ",\"axis1\":" + String(st.axis1, 4);
  j += ",\"axis2\":" + String(st.axis2, 4);
  j += ",\"pier\":\"" + String(st.pierSide == PIER_EAST ? "East" : "West") + "\"";
  j += ",\"state\":\"" + String(state) + "\"";
  j += ",\"tracking\":" + String(boolText(st.tracking));
  j += ",\"trackRate\":" + String(mount.trackRate());
  j += ",\"guiding\":" + String(boolText(st.guiding));
  j += ",\"atHome\":" + String(boolText(st.atHome));
  j += ",\"atPark\":" + String(boolText(st.atPark));
  j += ",\"homed\":" + String(boolText(st.homed));
  j += ",\"timeSet\":" + String(boolText(Astro::timeIsSet()));
  j += ",\"utc\":" + String(Astro::utcNow(), 0);
  j += ",\"drvRa\":" + String(boolText(st.driverOk[0]));
  j += ",\"drvDec\":" + String(boolText(st.driverOk[1]));
  j += ",\"swRa\":" + String(boolText(st.homeSwitch[0]));
  j += ",\"swDec\":" + String(boolText(st.homeSwitch[1]));
  j += ",\"message\":\"" + String(st.message) + "\"";
  j += "}";
  sendJson(j);
}

static double rateFor(const String &name) {
  if (name == "g") return settings.guideRate * SIDEREAL_DEG_S;
  if (name == "c") return CENTER_RATE_SIDEREAL * SIDEREAL_DEG_S;
  if (name == "m") return settings.slewRate * 0.25;
  return settings.slewRate;
}

static int dirFor(const String &name) {
  if (name == "n") return DIR_NORTH;
  if (name == "s") return DIR_SOUTH;
  if (name == "e") return DIR_EAST;
  if (name == "w") return DIR_WEST;
  return -1;
}

static void handleCommand() {
  String cmd = server.arg("cmd");
  if (cmd == "goto") {
    sendResult(mount.gotoEqu(server.arg("ra").toDouble(), server.arg("dec").toDouble()));
  } else if (cmd == "sync") {
    sendResult(mount.syncEqu(server.arg("ra").toDouble(), server.arg("dec").toDouble()));
  } else if (cmd == "track") {
    sendResult(mount.setTracking(server.arg("on") == "1"));
  } else if (cmd == "trackrate") {
    mount.setTrackRate(server.arg("rate").toInt());
    sendResult(M_OK);
  } else if (cmd == "move") {
    int dir = dirFor(server.arg("dir"));
    if (dir < 0) sendFail("Bad direction");
    else sendResult(mount.moveDir(dir, rateFor(server.arg("rate")), MOVE_TIMEOUT_MS));
  } else if (cmd == "stopmove") {
    int dir = dirFor(server.arg("dir"));
    mount.stopAxis((dir == DIR_NORTH || dir == DIR_SOUTH) ? 1 : 0);
    sendResult(M_OK);
  } else if (cmd == "stop") {
    mount.abort();
    sendResult(M_OK);
  } else if (cmd == "home") {
    sendResult(mount.findHome());
  } else if (cmd == "park") {
    sendResult(mount.park());
  } else if (cmd == "unpark") {
    mount.unpark();
    sendResult(M_OK);
  } else if (cmd == "sethome") {
    sendResult(mount.setHomeHere());
  } else if (cmd == "time") {
    double utc = server.arg("utc").toDouble();
    if (utc < 1.5e9) {
      sendFail("Bad time");
    } else {
      Astro::setUtc(utc);
      sendResult(M_OK);
    }
  } else if (cmd == "reboot") {
    sendResult(M_OK);
    rebootRequested = true;
    rebootAt = millis() + 500;
  } else {
    sendFail("Unknown command");
  }
}

static String jsonEscape(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

static void handleConfigGet() {
  String j = "{";
  j += "\"ssid\":\"" + jsonEscape(settings.ssid) + "\"";
  j += ",\"hostname\":\"" + jsonEscape(settings.hostname) + "\"";
  j += ",\"latitude\":" + String(settings.latitude, 5);
  j += ",\"longitude\":" + String(settings.longitude, 5);
  j += ",\"elevation\":" + String(settings.elevation, 1);
  j += ",\"guideRate\":" + String(settings.guideRate, 2);
  j += ",\"slewRate\":" + String(settings.slewRate, 2);
  j += ",\"maxSlewRate\":" + String((double)MAX_SLEW_RATE, 2);
  j += ",\"meridianLimit\":" + String(settings.meridianLimit, 1);
  j += ",\"ip\":\"" + (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\"";
  j += ",\"ap\":" + String(boolText(WiFi.getMode() == WIFI_AP));
  j += ",\"version\":\"" FIRMWARE_VERSION "\"";
  j += "}";
  sendJson(j);
}

static void handleConfigPost() {
  bool wifiChanged = false;
  if (server.hasArg("ssid") && server.arg("ssid") != settings.ssid) {
    settings.ssid = server.arg("ssid");
    settings.password = server.arg("password");
    wifiChanged = true;
  } else if (server.arg("password").length()) {
    settings.password = server.arg("password");
    wifiChanged = true;
  }
  if (server.hasArg("hostname") && server.arg("hostname") != settings.hostname) {
    settings.hostname = server.arg("hostname");
    wifiChanged = true;
  }
  if (server.hasArg("latitude")) settings.latitude = server.arg("latitude").toDouble();
  if (server.hasArg("longitude")) settings.longitude = server.arg("longitude").toDouble();
  if (server.hasArg("elevation")) settings.elevation = server.arg("elevation").toDouble();
  if (server.hasArg("guideRate")) settings.guideRate = server.arg("guideRate").toDouble();
  if (server.hasArg("slewRate")) settings.slewRate = server.arg("slewRate").toDouble();
  if (server.hasArg("meridianLimit")) settings.meridianLimit = server.arg("meridianLimit").toDouble();
  settings.save();
  sendJson(String("{\"ok\":true,\"reboot\":") + boolText(wifiChanged) + "}");
}

void begin() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/cmd", HTTP_POST, handleCommand);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.onNotFound([]() {
    if (!Alpaca::handleRequest()) server.send(404, "text/plain", "Not found");
  });
  Alpaca::begin();
  server.begin();
}

void poll() {
  server.handleClient();
  Alpaca::poll();
  if (rebootRequested && (int32_t)(millis() - rebootAt) >= 0) ESP.restart();
}

}  // namespace WebUI
