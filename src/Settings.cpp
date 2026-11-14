#include "Settings.h"
#include <Preferences.h>
#include "Config.h"

Settings settings;

static const char *NS = "mymount";

void Settings::sanitize() {
  latitude = constrain(latitude, -90.0, 90.0);
  longitude = constrain(longitude, -180.0, 180.0);
  guideRate = constrain(guideRate, 0.1, 1.0);
  slewRate = constrain(slewRate, 0.05, (double)MAX_SLEW_RATE);
  meridianLimit = constrain(meridianLimit, 0.0, 30.0);
  if (hostname.length() == 0) hostname = DEFAULT_HOSTNAME;
}

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
  p.end();
}
