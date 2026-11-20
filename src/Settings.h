// Runtime settings stored in flash (NVS). Defaults come from Config.h.
#pragma once
#include <Arduino.h>

// Per-axis settings. Index 0 = RA, 1 = Dec.
struct AxisSettings {
  uint16_t microsteps;   // 1..256, power of two          (applied at restart)
  double gearRatio;      // motor revs per axis rev        (applied at restart)
  bool invert;           // reverse the motor direction    (applied at restart)
  uint16_t currentMa;    // RMS run current                (applied immediately)
  int8_t homeDir;        // +1 / -1: home search direction (applied at the next home search)
  double homeAngle;      // axis angle when the switch triggers, deg (next home search)
};

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
  uint8_t logLevel;       // Log::Level: 0 debug, 1 info, 2 warning, 3 error
  AxisSettings axis[2];

  void load();
  void save();
  void sanitize();

  // Microsteps per degree of axis rotation for the given axis settings.
  static double stepsPerDeg(int axisIndex, const AxisSettings &a);
};

extern Settings settings;
