#include "WebUI.h"
#include <WiFi.h>
#include "Alpaca.h"
#include "Astro.h"
#include "Config.h"
#include "Log.h"
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
  if (name == "q") return settings.slewRate * 0.25;
  if (name == "h") return settings.slewRate * 0.5;
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
  } else if (cmd == "gotoaltaz") {
    sendResult(mount.gotoAltAz(server.arg("alt").toDouble(), server.arg("az").toDouble()));
  } else if (cmd == "syncaltaz") {
    sendResult(mount.syncAltAz(server.arg("alt").toDouble(), server.arg("az").toDouble()));
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
  } else if (cmd == "setpark") {
    sendResult(mount.setParkHere());
  } else if (cmd == "time") {
    double utc = server.arg("utc").toDouble();
    if (utc < 1.5e9) {
      sendFail("Bad time");
    } else {
      Astro::setUtc(utc);
      sendResult(M_OK);
    }
  } else if (cmd == "loglevel") {
    int level = server.arg("level").toInt();
    if (!server.hasArg("level") || level < (int)Log::Level::Debug || level > (int)Log::Level::Error) {
      sendFail("Bad log level");
    } else {
      static const char *NAMES[] = {"debug", "info", "warning", "error"};
      // Record the change under the more verbose of the old and new levels, so it shows either way.
      Log::Level newLevel = (Log::Level)level;
      if (newLevel < Log::level()) Log::setLevel(newLevel);
      LOGI("Log level set to %s", NAMES[level]);
      Log::setLevel(newLevel);
      settings.logLevel = (uint8_t)level;
      settings.save();
      sendResult(M_OK);
    }
  } else if (cmd == "logsource") {
    // Switches the generation of protocol traffic messages, not just their display.
    String src = server.arg("source");
    bool on = server.arg("on") == "1";
    if (!server.hasArg("on") || (src != "lx200" && src != "alpaca")) {
      sendFail("Bad log source");
    } else {
      bool lx = src == "lx200";
      Log::setSourceEnabled(lx ? Log::Source::Lx200 : Log::Source::Alpaca, on);
      (lx ? settings.logLx200 : settings.logAlpaca) = on;
      settings.save();
      LOGI("%s logging %s", lx ? "LX200" : "Alpaca", on ? "on" : "off");
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

// Log lines newer than 'since'. 'now' lets the page turn each line's millis() into a clock time.
static void handleLog() {
  uint32_t newest = Log::newest();
  uint32_t since = strtoul(server.arg("since").c_str(), nullptr, 10);
  if (since > newest) since = 0;   // the page is ahead of us: the controller has restarted
  uint32_t first = newest > Log::CAPACITY ? newest - Log::CAPACITY + 1 : 1;
  if (since >= first) first = since + 1;

  String j;
  j.reserve(256 + (newest + 1 - first) * 64);
  j += "{\"now\":" + String(millis()) + ",\"lines\":[";
  Log::Entry e;
  bool any = false;
  for (uint32_t seq = first; seq <= newest; seq++) {
    if (!Log::read(seq, e)) continue;   // overwritten while this reply was being built
    if (any) j += ',';
    any = true;
    j += "{\"t\":" + String(e.ms) + ",\"l\":" + String((int)e.level) + ",\"m\":\"" + jsonEscape(e.text) + "\"}";
  }
  j += "],\"next\":" + String(newest) + ",\"level\":" + String((int)Log::level());
  j += ",\"lx200\":" + String(Log::sourceEnabled(Log::Source::Lx200) ? "true" : "false");
  j += ",\"alpaca\":" + String(Log::sourceEnabled(Log::Source::Alpaca) ? "true" : "false") + "}";
  sendJson(j);
}

static void handleConfigGet() {
  SettingsLock lock;
  String j = "{";
  j += "\"ssid\":\"" + jsonEscape(settings.ssid) + "\"";
  j += ",\"hostname\":\"" + jsonEscape(settings.hostname) + "\"";
  j += ",\"latitude\":" + String(settings.latitude, 5);
  j += ",\"longitude\":" + String(settings.longitude, 5);
  j += ",\"elevation\":" + String(settings.elevation, 1);
  j += ",\"guideRate\":" + String(settings.guideRate, 2);
  j += ",\"slewRate\":" + String(settings.slewRate, 2);
  j += ",\"maxSlewRate\":" + String(min(mount.maxRate(0), mount.maxRate(1)), 2);
  j += ",\"meridianLimit\":" + String(settings.meridianLimit, 1);
  j += ",\"axes\":[";
  for (int i = 0; i < 2; i++) {
    const AxisSettings &a = settings.axis[i];
    if (i) j += ",";
    j += "{\"stepAngle\":" + String(i == 0 ? RA_MOTOR_STEP_ANGLE : DEC_MOTOR_STEP_ANGLE, 2);
    j += ",\"microsteps\":" + String(a.microsteps);
    j += ",\"gearRatio\":" + String(a.gearRatio, 4);
    j += ",\"invert\":" + String(boolText(a.invert));
    j += ",\"current\":" + String(a.currentMa);
    j += ",\"homeDir\":" + String(a.homeDir);
    j += ",\"homeAngle\":" + String(a.homeAngle, 3);
    j += ",\"maxRate\":" + String(mount.maxRate(i), 2) + "}";
  }
  j += "],\"restartNeeded\":" + String(boolText(mount.restartNeeded()));
  j += ",\"currentRange\":[" + String(MIN_RUN_CURRENT_MA) + "," + String(MAX_RUN_CURRENT_MA) + "]";
  j += ",\"gearRange\":[" + String(MIN_GEAR_RATIO, 0) + "," + String(MAX_GEAR_RATIO, 0) + "]";
  j += ",\"ip\":\"" + (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP() : WiFi.localIP()).toString() + "\"";
  j += ",\"ap\":" + String(boolText(WiFi.getMode() == WIFI_AP));
  j += ",\"version\":\"" FIRMWARE_VERSION "\"";
  j += "}";
  sendJson(j);
}

static void handleConfigPost() {
  SettingsLock lock;
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

  // Axis fields are named ra_<field> and dec_<field>.
  static const char *prefix[2] = {"ra_", "dec_"};
  for (int i = 0; i < 2; i++) {
    AxisSettings &a = settings.axis[i];
    String p = prefix[i];
    if (server.hasArg(p + "microsteps")) a.microsteps = (uint16_t)server.arg(p + "microsteps").toInt();
    if (server.hasArg(p + "gearRatio")) a.gearRatio = server.arg(p + "gearRatio").toDouble();
    if (server.hasArg(p + "invert")) a.invert = server.arg(p + "invert") == "1";
    if (server.hasArg(p + "current")) a.currentMa = (uint16_t)constrain(server.arg(p + "current").toInt(), 0, 65535);
    if (server.hasArg(p + "homeDir")) a.homeDir = server.arg(p + "homeDir").toInt() < 0 ? -1 : 1;
    if (server.hasArg(p + "homeAngle")) a.homeAngle = server.arg(p + "homeAngle").toDouble();
  }
  settings.save();         // also brings out-of-range values back into range
  mount.applyCurrents();

  bool axesChanged = mount.restartNeeded();
  LOGI("Configuration saved%s%s", wifiChanged ? " (network settings changed, restart to apply)" : "",
       axesChanged ? " (axis settings changed, restart to apply)" : "");
  sendJson(String("{\"ok\":true,\"reboot\":") + boolText(wifiChanged || axesChanged) + "}");
}

void begin() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", WEB_PAGE); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/cmd", HTTP_POST, handleCommand);
  server.on("/api/log", HTTP_GET, handleLog);
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
