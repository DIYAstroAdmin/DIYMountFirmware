// Clock, sidereal time and coordinate helpers.
#pragma once
#include <Arduino.h>

namespace Astro {

// --- clock (UTC, unix seconds). Safe to call from any task. ---
void   setUtc(double unixSeconds);
double utcNow();
bool   timeIsSet();
int64_t microsNow();                 // monotonic, no rollover

// --- sidereal time ---
double lstHours(double longitudeDeg);   // local apparent-ish sidereal time, 0..24

// --- angle helpers ---
double norm360(double deg);          // 0..360
double norm180(double deg);          // -180..180
double normHours(double h);          // 0..24

// hour angle/declination -> altitude/azimuth (all degrees, azimuth from north through east)
void haDecToAltAz(double haDeg, double decDeg, double latDeg, double &altDeg, double &azDeg);

// --- calendar ---
int64_t daysFromCivil(int y, int m, int d);                 // days since 1970-01-01
void    civilFromDays(int64_t days, int &y, int &m, int &d);

// --- sexagesimal parsing/formatting ---
// Parses "12:34:56", "+12*34'56", "12:34.5", "12.5" ... Returns false if no number found.
bool   parseSexagesimal(const char *s, double &value);
// Splits a value into sign, whole, minutes, seconds (rounded to whole seconds).
void   toDms(double value, bool &negative, int &d, int &m, int &s);

}  // namespace Astro
