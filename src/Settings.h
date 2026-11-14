// Runtime settings stored in flash (NVS). Defaults come from Config.h.
#pragma once
#include <Arduino.h>

struct Settings {
  String ssid;
  String password;
  String hostname;
  double latitude;        // deg, north positive
  double longitude;       // deg, east positive
  double elevation;       // m
  double guideRate;       // multiples of sidereal
  double slewRate;        // deg/s
  double meridianLimit;   // deg past the meridian

  void load();
  void save();
  void sanitize();
};

extern Settings settings;
