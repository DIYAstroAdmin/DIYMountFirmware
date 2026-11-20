#include "Settings.h"
#include <Preferences.h>
#include "Config.h"
#include "Log.h"

Settings settings;

static const char *NS = "mymount";

static const AxisSettings AXIS_DEFAULTS[2] = {
    {RA_MICROSTEPS, RA_GEAR_RATIO, RA_INVERT_DIR, RA_RUN_CURRENT_MA, RA_HOME_SEARCH_DIR, RA_HOME_SWITCH_ANGLE},
    {DEC_MICROSTEPS, DEC_GEAR_RATIO, DEC_INVERT_DIR, DEC_RUN_CURRENT_MA, DEC_HOME_SEARCH_DIR, DEC_HOME_SWITCH_ANGLE},
};
static const char *AXIS_PREFIX[2] = {"ra", "dec"};

double Settings::stepsPerDeg(int axisIndex, const AxisSettings &a) {
  double stepAngle = axisIndex == 0 ? RA_MOTOR_STEP_ANGLE : DEC_MOTOR_STEP_ANGLE;
  return (360.0 / stepAngle) * a.microsteps * a.gearRatio / 360.0;
}

static bool validMicrosteps(uint16_t m) { return m >= 1 && m <= 256 && (m & (m - 1)) == 0; }

void Settings::sanitize() {
  latitude = constrain(latitude, -90.0, 90.0);
  longitude = constrain(longitude, -180.0, 180.0);
  guideRate = constrain(guideRate, 0.1, 1.0);
  slewRate = constrain(slewRate, 0.05, (double)MAX_SLEW_RATE);
  meridianLimit = constrain(meridianLimit, 0.0, 30.0);
  if (logLevel > (uint8_t)Log::Level::Error) logLevel = (uint8_t)Log::Level::Info;
  if (hostname.length() == 0) hostname = DEFAULT_HOSTNAME;
  for (int i = 0; i < 2; i++) {
    AxisSettings &a = axis[i];
    if (!validMicrosteps(a.microsteps)) a.microsteps = AXIS_DEFAULTS[i].microsteps;
    if (!(a.gearRatio >= MIN_GEAR_RATIO && a.gearRatio <= MAX_GEAR_RATIO)) a.gearRatio = AXIS_DEFAULTS[i].gearRatio;
    a.currentMa = constrain(a.currentMa, (uint16_t)MIN_RUN_CURRENT_MA, (uint16_t)MAX_RUN_CURRENT_MA);
    if (a.homeDir != 1 && a.homeDir != -1) a.homeDir = AXIS_DEFAULTS[i].homeDir;
    if (!(a.homeAngle >= -180.0 && a.homeAngle <= 180.0)) a.homeAngle = AXIS_DEFAULTS[i].homeAngle;
  }
}

// NVS keys are limited to 15 characters: "dec.hang" etc. is well within that.
static String key(int i, const char *name) { return String(AXIS_PREFIX[i]) + "." + name; }

void Settings::load() {
  Preferences p;
  p.begin(NS, true);
  ssid = p.getString("ssid", DEFAULT_WIFI_SSID);
  password = p.getString("pass", DEFAULT_WIFI_PASSWORD);
  hostname = p.getString("host", DEFAULT_HOSTNAME);
  latitude = p.getDouble("lat", DEFAULT_LATITUDE);
  longitude = p.getDouble("lon", DEFAULT_LONGITUDE);
  elevation = p.getDouble("elev", DEFAULT_ELEVATION);
  guideRate = p.getDouble("guide", DEFAULT_GUIDE_RATE);
  slewRate = p.getDouble("slew", DEFAULT_SLEW_RATE);
  meridianLimit = p.getDouble("merid", DEFAULT_MERIDIAN_LIMIT);
  logLevel = p.getUChar("loglvl", (uint8_t)Log::Level::Info);
  for (int i = 0; i < 2; i++) {
    const AxisSettings &d = AXIS_DEFAULTS[i];
    AxisSettings &a = axis[i];
    a.microsteps = p.getUShort(key(i, "ms").c_str(), d.microsteps);
    a.gearRatio = p.getDouble(key(i, "gear").c_str(), d.gearRatio);
    a.invert = p.getBool(key(i, "inv").c_str(), d.invert);
    a.currentMa = p.getUShort(key(i, "cur").c_str(), d.currentMa);
    a.homeDir = p.getChar(key(i, "hdir").c_str(), d.homeDir);
    a.homeAngle = p.getDouble(key(i, "hang").c_str(), d.homeAngle);
  }
  p.end();
  sanitize();
}

void Settings::save() {
  sanitize();
  Preferences p;
  p.begin(NS, false);
  p.putString("ssid", ssid);
  p.putString("pass", password);
  p.putString("host", hostname);
  p.putDouble("lat", latitude);
  p.putDouble("lon", longitude);
  p.putDouble("elev", elevation);
  p.putDouble("guide", guideRate);
  p.putDouble("slew", slewRate);
  p.putDouble("merid", meridianLimit);
  p.putUChar("loglvl", logLevel);
  for (int i = 0; i < 2; i++) {
    const AxisSettings &a = axis[i];
    p.putUShort(key(i, "ms").c_str(), a.microsteps);
    p.putDouble(key(i, "gear").c_str(), a.gearRatio);
    p.putBool(key(i, "inv").c_str(), a.invert);
    p.putUShort(key(i, "cur").c_str(), a.currentMa);
    p.putChar(key(i, "hdir").c_str(), a.homeDir);
    p.putDouble(key(i, "hang").c_str(), a.homeAngle);
  }
  p.end();
}
