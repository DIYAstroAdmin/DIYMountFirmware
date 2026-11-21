#include "Astro.h"
#include <esp_timer.h>
#include <math.h>

namespace Astro {

static portMUX_TYPE clockMux = portMUX_INITIALIZER_UNLOCKED;
// UTC (microseconds since 1970) minus the monotonic timer. Starts at 2026-01-01 until set.
static int64_t clockOffsetUs = 1767225600LL * 1000000LL;
static bool    clockSet = false;

int64_t microsNow() { return esp_timer_get_time(); }

void setUtc(double unixSeconds) {
  int64_t off = (int64_t)(unixSeconds * 1e6) - microsNow();
  portENTER_CRITICAL(&clockMux);
  clockOffsetUs = off;
  clockSet = true;
  portEXIT_CRITICAL(&clockMux);
}

double utcNow() {
  portENTER_CRITICAL(&clockMux);
  int64_t off = clockOffsetUs;
  portEXIT_CRITICAL(&clockMux);
  return (double)(off + microsNow()) / 1e6;
}

bool timeIsSet() { return clockSet; }

double norm360(double deg) {
  deg = fmod(deg, 360.0);
  if (deg < 0) deg += 360.0;
  return deg;
}

double norm180(double deg) {
  deg = norm360(deg);
  if (deg >= 180.0) deg -= 360.0;
  return deg;
}

double normHours(double h) {
  h = fmod(h, 24.0);
  if (h < 0) h += 24.0;
  return h;
}

double lstHours(double longitudeDeg) {
  double jd = utcNow() / 86400.0 + 2440587.5;
  double d = jd - 2451545.0;
  double gmst = 18.697374558 + 24.06570982441908 * d;
  return normHours(gmst + longitudeDeg / 15.0);
}

void haDecToAltAz(double haDeg, double decDeg, double latDeg, double &altDeg, double &azDeg) {
  const double r = M_PI / 180.0;
  double h = haDeg * r, d = decDeg * r, p = latDeg * r;
  double sinAlt = sin(p) * sin(d) + cos(p) * cos(d) * cos(h);
  if (sinAlt > 1) sinAlt = 1;
  if (sinAlt < -1) sinAlt = -1;
  altDeg = asin(sinAlt) / r;
  double y = -cos(d) * sin(h);
  double x = sin(d) * cos(p) - cos(d) * cos(h) * sin(p);
  azDeg = norm360(atan2(y, x) / r);
}

// The transform is its own inverse with hour angle <-> azimuth and declination <-> altitude.
void altAzToHaDec(double altDeg, double azDeg, double latDeg, double &haDeg, double &decDeg) {
  haDecToAltAz(azDeg, altDeg, latDeg, decDeg, haDeg);
  haDeg = norm180(haDeg);
}

// Howard Hinnant's civil calendar algorithms.
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

void civilFromDays(int64_t z, int &y, int &m, int &d) {
  z += 719468;
  int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int64_t yy = (int64_t)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  d = (int)(doy - (153 * mp + 2) / 5 + 1);
  m = (int)(mp < 10 ? mp + 3 : mp - 9);
  y = (int)(yy + (m <= 2));
}

bool parseSexagesimal(const char *s, double &value) {
  while (*s == ' ') s++;
  bool neg = false;
  if (*s == '+' || *s == '-') {
    neg = (*s == '-');
    s++;
  }
  double parts[3] = {0, 0, 0};
  int n = 0;
  while (n < 3) {
    while (*s && !isdigit((unsigned char)*s) && *s != '.') s++;   // skip separators
    if (!*s) break;
    char *end;
    parts[n] = strtod(s, &end);
    if (end == s) break;
    s = end;
    n++;
  }
  if (n == 0) return false;
  value = parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
  if (neg) value = -value;
  return true;
}

void toDms(double value, bool &negative, int &d, int &m, int &s) {
  negative = value < 0;
  long total = lround(fabs(value) * 3600.0);
  d = (int)(total / 3600);
  m = (int)((total % 3600) / 60);
  s = (int)(total % 60);
  if (total == 0) negative = false;
}

}  // namespace Astro
