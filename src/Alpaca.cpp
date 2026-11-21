#include "Alpaca.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "Astro.h"
#include "Config.h"
#include "Log.h"
#include "Mount.h"
#include "Settings.h"
#include "WebUI.h"

namespace Alpaca {

static const int DISCOVERY_PORT = 32227;
static const double SIDEREAL_DEG_S = 360.0 / 86164.0905;

// ASCOM error numbers
static const int E_NOT_IMPLEMENTED = 0x400;
static const int E_INVALID_VALUE = 0x401;
static const int E_VALUE_NOT_SET = 0x402;
static const int E_PARKED = 0x408;
static const int E_INVALID_OPERATION = 0x40B;

static WiFiUDP udp;
static uint32_t serverTransaction = 0;
static bool connected = false;
static double slewSettleTime = 0;

// ---------------------------------------------------------------------------
// Request/response helpers
// ---------------------------------------------------------------------------
static bool findArg(const char *name, String &out) {
  for (int i = 0; i < server.args(); i++) {
    if (server.argName(i).equalsIgnoreCase(name)) {
      out = server.arg(i);
      return true;
    }
  }
  return false;
}

static String jsonString(const String &s) {
  String out = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') out += '\\';
    if ((uint8_t)c >= 0x20) out += c;
  }
  out += '"';
  return out;
}

// Logs the current request with the outcome, e.g.
//   Alpaca PUT slewtocoordinatesasync RightAscension=5.5 Declination=20 => OK
// The client bookkeeping parameters are left out to keep lines short.
static void logExchange(const String &outcome) {
  if (!LOG_SOURCE_ON(Log::Source::Alpaca)) return;
  static const char *PREFIX = "/api/v1/telescope/0/";
  String line = server.method() == HTTP_PUT ? "Alpaca PUT " : "Alpaca GET ";
  String uri = server.uri();
  if (uri.length() > strlen(PREFIX) && uri.substring(0, strlen(PREFIX)).equalsIgnoreCase(PREFIX))
    uri = uri.substring(strlen(PREFIX));
  line += uri;
  for (int i = 0; i < server.args(); i++) {
    const String name = server.argName(i);
    if (name.equalsIgnoreCase("ClientID") || name.equalsIgnoreCase("ClientTransactionID") || name == "plain")
      continue;
    line += ' ';
    line += name;
    line += '=';
    line += server.arg(i);
  }
  LOG_ALPACA("%s => %s", line.c_str(), outcome.c_str());
}

// value: JSON text for "Value", or empty for methods that return nothing.
static void sendResponse(const String &value, int errNum = 0, const char *errMsg = "") {
  if (errNum) logExchange("error 0x" + String(errNum, HEX) + " " + errMsg);
  else logExchange(value.length() ? value : String("OK"));
  String id;
  uint32_t clientTransaction = 0;
  if (findArg("ClientTransactionID", id)) clientTransaction = strtoul(id.c_str(), nullptr, 10);
  String body = "{";
  if (value.length() && errNum == 0) body += "\"Value\":" + value + ",";
  body += "\"ClientTransactionID\":" + String(clientTransaction);
  body += ",\"ServerTransactionID\":" + String(++serverTransaction);
  body += ",\"ErrorNumber\":" + String(errNum);
  body += ",\"ErrorMessage\":" + jsonString(errMsg) + "}";
  server.send(200, "application/json", body);
}

static void sendOk() { sendResponse(""); }
static void sendError(int num, const char *msg) { sendResponse("", num, msg); }
static void sendBool(bool v) { sendResponse(v ? "true" : "false"); }
static void sendInt(long v) { sendResponse(String(v)); }
static void sendDouble(double v) { sendResponse(String(v, 8)); }
static void sendString(const String &v) { sendResponse(jsonString(v)); }
static void notImplemented() { sendError(E_NOT_IMPLEMENTED, "Not implemented"); }
static void badRequest(const String &msg) {
  logExchange("HTTP 400 " + msg);
  server.send(400, "text/plain", msg);
}

// Parameter readers. On failure they send HTTP 400 and return false.
static bool numArg(const char *name, double &v) {
  String s;
  if (findArg(name, s)) {
    s.trim();
    char *end;
    v = strtod(s.c_str(), &end);
    if (s.length() && *end == 0 && isfinite(v)) return true;
  }
  badRequest(String("Missing or invalid parameter: ") + name);
  return false;
}

static bool boolArg(const char *name, bool &v) {
  String s;
  if (findArg(name, s)) {
    s.trim();
    if (s.equalsIgnoreCase("true")) { v = true; return true; }
    if (s.equalsIgnoreCase("false")) { v = false; return true; }
  }
  badRequest(String("Missing or invalid parameter: ") + name);
  return false;
}

static void sendMountResult(MountError e) {
  switch (e) {
    case M_OK: sendOk(); break;
    case M_PARKED: sendError(E_PARKED, Mount::errorText(e)); break;
    case M_NO_TARGET: sendError(E_VALUE_NOT_SET, Mount::errorText(e)); break;
    case M_BUSY: sendError(E_INVALID_OPERATION, Mount::errorText(e)); break;
    default: sendError(E_INVALID_VALUE, Mount::errorText(e)); break;
  }
}

static String isoUtc() {
  double utc = Astro::utcNow();
  double day = floor(utc / 86400.0);
  double secs = utc - day * 86400.0;
  int y, mo, d;
  Astro::civilFromDays((int64_t)day, y, mo, d);
  int h = (int)(secs / 3600.0);
  int mi = (int)((secs - h * 3600.0) / 60.0);
  double s = secs - h * 3600.0 - mi * 60.0;
  char out[40];
  snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d:%06.3fZ", y, mo, d, h, mi, s > 59.999 ? 59.999 : s);
  return out;
}

// ---------------------------------------------------------------------------
// GET
// ---------------------------------------------------------------------------
static bool handleGet(const String &m) {
  MountStatus st;
  mount.getStatus(st);

  // Common device properties
  if (m == "connected") sendBool(connected);
  else if (m == "connecting") sendBool(false);
  else if (m == "description") sendString("MyMount equatorial mount controller");
  else if (m == "driverinfo") sendString("MyMount firmware for FYSETC E4");
  else if (m == "driverversion") sendString(FIRMWARE_VERSION);
  else if (m == "interfaceversion") sendInt(4);
  else if (m == "name") sendString(FIRMWARE_NAME);
  else if (m == "supportedactions") sendResponse("[]");
  else if (m == "devicestate") {
    String v = "[";
    v += "{\"Name\":\"Altitude\",\"Value\":" + String(st.alt, 6) + "},";
    v += "{\"Name\":\"AtHome\",\"Value\":" + String(st.atHome ? "true" : "false") + "},";
    v += "{\"Name\":\"AtPark\",\"Value\":" + String(st.atPark ? "true" : "false") + "},";
    v += "{\"Name\":\"Azimuth\",\"Value\":" + String(st.az, 6) + "},";
    v += "{\"Name\":\"Declination\",\"Value\":" + String(st.dec, 6) + "},";
    v += "{\"Name\":\"IsPulseGuiding\",\"Value\":" + String(st.guiding ? "true" : "false") + "},";
    v += "{\"Name\":\"RightAscension\",\"Value\":" + String(st.ra, 8) + "},";
    v += "{\"Name\":\"SideOfPier\",\"Value\":" + String(st.pierSide) + "},";
    v += "{\"Name\":\"SiderealTime\",\"Value\":" + String(st.lst, 8) + "},";
    v += "{\"Name\":\"Slewing\",\"Value\":" + String(st.slewing ? "true" : "false") + "},";
    v += "{\"Name\":\"Tracking\",\"Value\":" + String(st.tracking ? "true" : "false") + "},";
    v += "{\"Name\":\"UTCDate\",\"Value\":" + jsonString(isoUtc()) + "},";
    v += "{\"Name\":\"TimeStamp\",\"Value\":" + jsonString(isoUtc()) + "}]";
    sendResponse(v);
  }

  // Capabilities
  else if (m == "alignmentmode") sendInt(2);            // German polar
  else if (m == "equatorialsystem") sendInt(1);         // topocentric (JNow)
  else if (m == "canfindhome") sendBool(true);
  else if (m == "canpark") sendBool(true);
  else if (m == "canunpark") sendBool(true);
  else if (m == "canpulseguide") sendBool(true);
  else if (m == "cansetguiderates") sendBool(true);
  else if (m == "cansettracking") sendBool(true);
  else if (m == "canslewasync") sendBool(true);
  else if (m == "cansync") sendBool(true);
  else if (m == "canslewaltazasync") sendBool(true);
  else if (m == "cansyncaltaz") sendBool(true);
  else if (m == "canslew" || m == "canslewaltaz" || m == "cansetpark" || m == "cansetpierside" || m == "cansetdeclinationrate" ||
           m == "cansetrightascensionrate" || m == "doesrefraction")
    sendBool(false);
  else if (m == "canmoveaxis") {
    double axis;
    if (!numArg("Axis", axis)) return true;
    if (axis < 0 || axis > 2) sendError(E_INVALID_VALUE, "Invalid axis");
    else sendBool(axis < 2);
  } else if (m == "axisrates") {
    double axis;
    if (!numArg("Axis", axis)) return true;
    if (axis < 0 || axis > 2) sendError(E_INVALID_VALUE, "Invalid axis");
    else if (axis < 2) sendResponse("[{\"Maximum\":" + String(mount.maxRate((int)axis), 4) + ",\"Minimum\":0.0}]");
    else sendResponse("[]");
  } else if (m == "trackingrates") sendResponse("[0,1,2]");

  // Position and state
  else if (m == "rightascension") sendDouble(st.ra);
  else if (m == "declination") sendDouble(st.dec);
  else if (m == "altitude") sendDouble(st.alt);
  else if (m == "azimuth") sendDouble(st.az);
  else if (m == "siderealtime") sendDouble(st.lst);
  else if (m == "sideofpier") sendInt(st.pierSide);
  else if (m == "athome") sendBool(st.atHome);
  else if (m == "atpark") sendBool(st.atPark);
  else if (m == "slewing") sendBool(st.slewing);
  else if (m == "ispulseguiding") sendBool(st.guiding);
  else if (m == "tracking") sendBool(st.tracking);
  else if (m == "trackingrate") sendInt(mount.trackRate());
  else if (m == "declinationrate" || m == "rightascensionrate") sendDouble(0);
  else if (m == "guideratedeclination" || m == "guideraterightascension")
    sendDouble(settings.guideRate * SIDEREAL_DEG_S);
  else if (m == "slewsettletime") sendInt((long)slewSettleTime);
  else if (m == "targetrightascension") {
    double v;
    if (mount.getTargetRa(v)) sendDouble(v);
    else sendError(E_VALUE_NOT_SET, "Target right ascension not set");
  } else if (m == "targetdeclination") {
    double v;
    if (mount.getTargetDec(v)) sendDouble(v);
    else sendError(E_VALUE_NOT_SET, "Target declination not set");
  } else if (m == "destinationsideofpier") {
    double ra, dec;
    if (!numArg("RightAscension", ra) || !numArg("Declination", dec)) return true;
    if (ra < 0 || ra >= 24 || dec < -90 || dec > 90) sendError(E_INVALID_VALUE, "Coordinates out of range");
    else sendInt(mount.destinationSideOfPier(ra, dec));
  }

  // Site and time
  else if (m == "sitelatitude") sendDouble(settings.latitude);
  else if (m == "sitelongitude") sendDouble(settings.longitude);
  else if (m == "siteelevation") sendDouble(settings.elevation);
  else if (m == "utcdate") sendString(isoUtc());

  // Optics are unknown to the mount
  else if (m == "aperturearea" || m == "aperturediameter" || m == "focallength") notImplemented();
  else return false;
  return true;
}

// ---------------------------------------------------------------------------
// PUT
// ---------------------------------------------------------------------------
static bool coordArgs(double &ra, double &dec) {
  if (!numArg("RightAscension", ra) || !numArg("Declination", dec)) return false;
  if (ra < 0 || ra >= 24 || dec < -90 || dec > 90) {
    sendError(E_INVALID_VALUE, "Coordinates out of range");
    return false;
  }
  return true;
}

static bool requireTracking() {
  MountStatus st;
  mount.getStatus(st);
  if (st.atPark) {
    sendError(E_PARKED, "Mount is parked");
    return false;
  }
  if (!st.tracking) {
    sendError(E_INVALID_OPERATION, "Tracking is off");
    return false;
  }
  return true;
}

static bool altAzArgs(double &alt, double &az) {
  if (!numArg("Azimuth", az) || !numArg("Altitude", alt)) return false;
  if (alt < -90 || alt > 90 || az < 0 || az >= 360) {
    sendError(E_INVALID_VALUE, "Coordinates out of range");
    return false;
  }
  return true;
}

// Alt/az slews and syncs are only allowed with tracking off.
static bool requireNotTracking() {
  MountStatus st;
  mount.getStatus(st);
  if (st.atPark) {
    sendError(E_PARKED, "Mount is parked");
    return false;
  }
  if (st.tracking) {
    sendError(E_INVALID_OPERATION, "Tracking is on");
    return false;
  }
  return true;
}

static bool handlePut(const String &m) {
  double v;
  bool b;

  if (m == "connected") {
    if (boolArg("Connected", b)) { connected = b; sendOk(); }
  } else if (m == "connect") { connected = true; sendOk(); }
  else if (m == "disconnect") { connected = false; sendOk(); }

  else if (m == "tracking") {
    if (boolArg("Tracking", b)) sendMountResult(mount.setTracking(b));
  } else if (m == "trackingrate") {
    if (!numArg("TrackingRate", v)) return true;
    if (v < TRACK_SIDEREAL || v > TRACK_SOLAR) sendError(E_INVALID_VALUE, "Unsupported tracking rate");
    else { mount.setTrackRate((int)v); sendOk(); }
  } else if (m == "targetrightascension") {
    if (!numArg("TargetRightAscension", v)) return true;
    if (mount.setTargetRa(v)) sendOk();
    else sendError(E_INVALID_VALUE, "Right ascension out of range");
  } else if (m == "targetdeclination") {
    if (!numArg("TargetDeclination", v)) return true;
    if (mount.setTargetDec(v)) sendOk();
    else sendError(E_INVALID_VALUE, "Declination out of range");
  } else if (m == "sitelatitude") {
    if (!numArg("SiteLatitude", v)) return true;
    if (v < -90 || v > 90) sendError(E_INVALID_VALUE, "Latitude out of range");
    else { settings.latitude = v; settings.save(); sendOk(); }
  } else if (m == "sitelongitude") {
    if (!numArg("SiteLongitude", v)) return true;
    if (v < -180 || v > 180) sendError(E_INVALID_VALUE, "Longitude out of range");
    else { settings.longitude = v; settings.save(); sendOk(); }
  } else if (m == "siteelevation") {
    if (!numArg("SiteElevation", v)) return true;
    if (v < -300 || v > 10000) sendError(E_INVALID_VALUE, "Elevation out of range");
    else { settings.elevation = v; settings.save(); sendOk(); }
  } else if (m == "guideratedeclination" || m == "guideraterightascension") {
    if (!numArg(m == "guideratedeclination" ? "GuideRateDeclination" : "GuideRateRightAscension", v)) return true;
    double x = v / SIDEREAL_DEG_S;
    if (x < 0.1 || x > 1.0) sendError(E_INVALID_VALUE, "Guide rate must be 0.1 to 1.0 x sidereal");
    else { settings.guideRate = x; sendOk(); }   // one rate for both axes, not saved to flash
  } else if (m == "slewsettletime") {
    if (!numArg("SlewSettleTime", v)) return true;
    if (v < 0) sendError(E_INVALID_VALUE, "Settle time must not be negative");
    else { slewSettleTime = v; sendOk(); }
  } else if (m == "utcdate") {
    String s;
    int y, mo, d, h, mi;
    double sec;
    if (!findArg("UTCDate", s) || sscanf(s.c_str(), "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) != 6) {
      badRequest("Missing or invalid parameter: UTCDate");
      return true;
    }
    Astro::setUtc((double)Astro::daysFromCivil(y, mo, d) * 86400.0 + h * 3600.0 + mi * 60.0 + sec);
    sendOk();
  }

  // Motion
  else if (m == "abortslew") {
    MountStatus st;
    mount.getStatus(st);
    if (st.atPark) sendError(E_PARKED, "Mount is parked");
    else { mount.abort(); sendOk(); }
  } else if (m == "findhome") sendMountResult(mount.findHome());
  else if (m == "park") sendMountResult(mount.park());
  else if (m == "unpark") { mount.unpark(); sendOk(); }
  else if (m == "moveaxis") {
    double axis, rate;
    if (!numArg("Axis", axis) || !numArg("Rate", rate)) return true;
    if (axis < 0 || axis > 1) sendError(E_INVALID_VALUE, "Invalid axis");
    else sendMountResult(mount.moveAxis((int)axis, rate));
  } else if (m == "pulseguide") {
    double dir, duration;
    if (!numArg("Direction", dir) || !numArg("Duration", duration)) return true;
    if (dir < 0 || dir > 3 || duration < 0) sendError(E_INVALID_VALUE, "Invalid direction or duration");
    else sendMountResult(mount.pulseGuide((int)dir, (uint32_t)duration));
  } else if (m == "slewtocoordinatesasync") {
    double ra, dec;
    if (!coordArgs(ra, dec) || !requireTracking()) return true;
    mount.setTargetRa(ra);
    mount.setTargetDec(dec);
    sendMountResult(mount.gotoEqu(ra, dec));
  } else if (m == "slewtotargetasync") {
    if (!requireTracking()) return true;
    sendMountResult(mount.gotoTarget());
  } else if (m == "synctocoordinates") {
    double ra, dec;
    if (!coordArgs(ra, dec) || !requireTracking()) return true;
    mount.setTargetRa(ra);
    mount.setTargetDec(dec);
    sendMountResult(mount.syncEqu(ra, dec));
  } else if (m == "synctotarget") {
    if (!requireTracking()) return true;
    sendMountResult(mount.syncTarget());
  } else if (m == "slewtoaltazasync") {
    double alt, az;
    if (!altAzArgs(alt, az) || !requireNotTracking()) return true;
    sendMountResult(mount.gotoAltAz(alt, az));
  } else if (m == "synctoaltaz") {
    double alt, az;
    if (!altAzArgs(alt, az) || !requireNotTracking()) return true;
    sendMountResult(mount.syncAltAz(alt, az));
  }

  // Not supported
  else if (m == "slewtocoordinates" || m == "slewtotarget" || m == "slewtoaltaz" || m == "setpark" || m == "declinationrate" || m == "rightascensionrate" ||
           m == "doesrefraction" || m == "sideofpier" || m == "action" || m == "commandblind" ||
           m == "commandbool" || m == "commandstring")
    notImplemented();
  else return false;
  return true;
}

bool handleRequest() {
  String uri = server.uri();
  uri.toLowerCase();
  const String prefix = "/api/v1/telescope/";
  if (!uri.startsWith(prefix)) return false;

  String rest = uri.substring(prefix.length());
  int slash = rest.indexOf('/');
  if (slash < 0 || rest.substring(0, slash) != "0") {
    badRequest("Unknown device");
    return true;
  }
  String method = rest.substring(slash + 1);

  bool handled = false;
  if (server.method() == HTTP_GET) handled = handleGet(method);
  else if (server.method() == HTTP_PUT) handled = handlePut(method);
  if (!handled) badRequest("Unknown method: " + method);
  return true;
}

// ---------------------------------------------------------------------------
// Management API and discovery
// ---------------------------------------------------------------------------
void begin() {
  server.on("/management/apiversions", HTTP_GET, []() { sendResponse("[1]"); });
  server.on("/management/v1/description", HTTP_GET, []() {
    sendResponse("{\"ServerName\":\"" FIRMWARE_NAME "\",\"Manufacturer\":\"MyMount\","
                 "\"ManufacturerVersion\":\"" FIRMWARE_VERSION "\",\"Location\":" +
                 jsonString(settings.hostname) + "}");
  });
  server.on("/management/v1/configureddevices", HTTP_GET, []() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    sendResponse("[{\"DeviceName\":\"" FIRMWARE_NAME "\",\"DeviceType\":\"Telescope\",\"DeviceNumber\":0,"
                 "\"UniqueID\":\"mymount-" + mac + "\"}]");
  });
  auto toHome = []() {
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "");
  };
  server.on("/setup", HTTP_GET, toHome);
  server.on("/setup/v1/telescope/0/setup", HTTP_GET, toHome);

  udp.begin(DISCOVERY_PORT);
}

void poll() {
  int size = udp.parsePacket();
  if (size <= 0) return;
  char buf[32];
  int n = udp.read(buf, sizeof(buf) - 1);
  if (n >= 16 && strncmp(buf, "alpacadiscovery1", 16) == 0) {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("{\"AlpacaPort\":80}");
    udp.endPacket();
  }
}

}  // namespace Alpaca
