#include "LX200.h"
#include <stdarg.h>
#include "Astro.h"
#include "Config.h"
#include "Log.h"
#include "Mount.h"
#include "Settings.h"

static const double SIDEREAL_DEG_S = 360.0 / 86164.0905;

// Writes to the serial port and keeps a copy of the reply for the log.
void LX200::send(const char *text) {
  _s.print(text);
  size_t room = sizeof(_reply) - 1 - _replyLen;
  size_t n = strlen(text);
  if (n > room) n = room;
  memcpy(_reply + _replyLen, text, n);
  _replyLen += n;
  _reply[_replyLen] = 0;
}

void LX200::send(char c) {
  char text[2] = {c, 0};
  send(text);
}

void LX200::sendf(const char *fmt, ...) {
  char out[80];
  va_list args;
  va_start(args, fmt);
  vsnprintf(out, sizeof(out), fmt, args);
  va_end(args);
  send(out);
}

void LX200::poll() {
  while (_s.available()) {
    char c = (char)_s.read();
    if (!_inCmd) {
      if (c == 0x06) {
        beginReply();
        send('G');          // ACK: German equatorial
        LOGD("<ACK> => %s", _reply);
      } else if (c == ':') {
        _inCmd = true;
        _len = 0;
      }
      continue;
    }
    if (c == '#') {
      _buf[_len] = 0;
      _inCmd = false;
      if (_len) execute(_buf);
    } else if (c == ':' && _len == 0) {
      // repeated start character
    } else if (_len < sizeof(_buf) - 1) {
      _buf[_len++] = c;
    } else {
      _inCmd = false;           // overlong: discard
    }
  }
}

double LX200::moveRate() const {
  switch (_rate) {
    case 'G': return settings.guideRate * SIDEREAL_DEG_S;
    case 'C': return CENTER_RATE_SIDEREAL * SIDEREAL_DEG_S;
    case 'M': return settings.slewRate * 0.25;
    default: return settings.slewRate;
  }
}

double LX200::localNow() const { return Astro::utcNow() - _utcOffset * 3600.0; }

static int dirFromChar(char c) {
  switch (c) {
    case 'n': return DIR_NORTH;
    case 's': return DIR_SOUTH;
    case 'e': return DIR_EAST;
    case 'w': return DIR_WEST;
  }
  return -1;
}

void LX200::beginReply() {
  _replyLen = 0;
  _reply[0] = 0;
}

void LX200::execute(const char *cmd) {
  beginReply();
  dispatch(cmd);
  LOGD(":%s# => %s", cmd, _reply);
}

void LX200::dispatch(const char *cmd) {
  switch (cmd[0]) {
    case 'G': cmdGet(cmd); break;
    case 'S': cmdSet(cmd); break;
    case 'M': cmdMove(cmd); break;

    case 'Q': {                              // :Q# stop all, :Qn# etc. stop one direction
      int dir = dirFromChar(cmd[1]);
      if (dir < 0) {
        mount.abort();
      } else {
        mount.stopAxis((dir == DIR_NORTH || dir == DIR_SOUTH) ? 1 : 0);
      }
      break;
    }

    case 'R':                                // :RG# :RC# :RM# :RS# manual move rate
      if (cmd[1] == 'G' || cmd[1] == 'C' || cmd[1] == 'M' || cmd[1] == 'S') _rate = cmd[1];
      break;

    case 'C':                                // :CM# sync to target, :CS# silent sync
      if (cmd[1] == 'M') {
        mount.syncTarget();
        send("Coordinates     matched.        #");
      } else if (cmd[1] == 'S') {
        mount.syncTarget();
      }
      break;

    case 'D': {                              // :D# slew progress bar
      MountStatus st;
      mount.getStatus(st);
      send(st.state != MountState::Idle ? "\x7f#" : "#");
      break;
    }

    case 'T':                                // tracking control
      switch (cmd[1]) {
        case 'e': send(mount.setTracking(true) == M_OK ? '1' : '0'); break;
        case 'd': mount.setTracking(false); send('1'); break;
        case 'Q': mount.setTrackRate(TRACK_SIDEREAL); break;
        case 'L': mount.setTrackRate(TRACK_LUNAR); break;
        case 'S': mount.setTrackRate(TRACK_SOLAR); break;
      }
      break;

    case 'h':                                // home / park
      switch (cmd[1]) {
        case 'P': mount.park(); break;       // slew to the home position and stop
        case 'F': mount.findHome(); break;   // seek the home switches
        case 'W': mount.unpark(); break;
        case 'R': mount.unpark(); send('1'); break;
      }
      break;

    default:                                 // :U# and anything else: no reply
      break;
  }
}

void LX200::cmdMove(const char *cmd) {
  switch (cmd[1]) {
    case 'S': {                              // :MS# slew to target
      MountError e = mount.gotoTarget();
      if (e == M_OK) {
        send('0');
      } else if (e == M_BELOW_HORIZON) {
        send("1Object below horizon.#");
      } else {
        sendf("2%s.#", Mount::errorText(e));
      }
      break;
    }
    case 'g': {                              // :Mgn1000# pulse guide, milliseconds
      int dir = dirFromChar(cmd[2]);
      if (dir >= 0) mount.pulseGuide(dir, (uint32_t)constrain(atoi(cmd + 3), 0, 30000));
      break;
    }
    default: {                               // :Mn# :Ms# :Me# :Mw# move until :Q
      int dir = dirFromChar(cmd[1]);
      if (dir >= 0) mount.moveDir(dir, moveRate());
      break;
    }
  }
}

void LX200::cmdGet(const char *cmd) {
  MountStatus st;
  bool neg;
  int d, m, s;
  switch (cmd[1]) {
    case 'R':                                // right ascension
      mount.getStatus(st);
      Astro::toDms(st.ra, neg, d, m, s);
      sendf("%02d:%02d:%02d#", d % 24, m, s);
      break;
    case 'D':                                // declination
      mount.getStatus(st);
      Astro::toDms(st.dec, neg, d, m, s);
      sendf("%c%02d*%02d'%02d#", neg ? '-' : '+', d, m, s);
      break;
    case 'r': {                              // target RA
      double ra = 0;
      mount.getTargetRa(ra);
      Astro::toDms(ra, neg, d, m, s);
      sendf("%02d:%02d:%02d#", d % 24, m, s);
      break;
    }
    case 'd': {                              // target Dec
      double dec = 0;
      mount.getTargetDec(dec);
      Astro::toDms(dec, neg, d, m, s);
      sendf("%c%02d*%02d'%02d#", neg ? '-' : '+', d, m, s);
      break;
    }
    case 'A':                                // altitude
      mount.getStatus(st);
      Astro::toDms(st.alt, neg, d, m, s);
      sendf("%c%02d*%02d'%02d#", neg ? '-' : '+', d, m, s);
      break;
    case 'Z':                                // azimuth
      mount.getStatus(st);
      Astro::toDms(st.az, neg, d, m, s);
      sendf("%03d*%02d'%02d#", d % 360, m, s);
      break;
    case 'S':                                // sidereal time
      mount.getStatus(st);
      Astro::toDms(st.lst, neg, d, m, s);
      sendf("%02d:%02d:%02d#", d % 24, m, s);
      break;
    case 'L': {                              // local time
      double tod = fmod(localNow(), 86400.0);
      if (tod < 0) tod += 86400.0;
      Astro::toDms(tod / 3600.0, neg, d, m, s);
      sendf("%02d:%02d:%02d#", d % 24, m, s);
      break;
    }
    case 'C': {                              // local date
      int y, mo, da;
      Astro::civilFromDays((int64_t)floor(localNow() / 86400.0), y, mo, da);
      sendf("%02d/%02d/%02d#", mo, da, y % 100);
      break;
    }
    case 'G':                                // UTC offset
      sendf("%+05.1f#", _utcOffset);
      break;
    case 't':                                // site latitude
      Astro::toDms(settings.latitude, neg, d, m, s);
      sendf("%c%02d*%02d#", neg ? '-' : '+', d, m);
      break;
    case 'g': {                              // site longitude, west positive
      Astro::toDms(-settings.longitude, neg, d, m, s);
      sendf("%c%03d*%02d#", neg ? '-' : '+', d, m);
      break;
    }
    case 'W':                                // mount status
      mount.getStatus(st);
      sendf("G%c%c#", st.tracking ? 'T' : 'N', st.atPark ? 'P' : '1');
      break;
    case 'm':                                // pier side
      mount.getStatus(st);
      send(st.pierSide == PIER_EAST ? "E#" : "W#");
      break;
    case 'V':                                // firmware information
      switch (cmd[2]) {
        case 'P': send(FIRMWARE_NAME "#"); break;
        case 'N': send(FIRMWARE_VERSION "#"); break;
        case 'D': send(__DATE__ "#"); break;
        case 'T': send(__TIME__ "#"); break;
      }
      break;
  }
}

void LX200::cmdSet(const char *cmd) {
  const char *arg = cmd + 2;
  double v;
  switch (cmd[1]) {
    case 'r':                                // :SrHH:MM:SS# target RA
      send(Astro::parseSexagesimal(arg, v) && mount.setTargetRa(v) ? '1' : '0');
      break;
    case 'd':                                // :SdsDD*MM:SS# target Dec
      send(Astro::parseSexagesimal(arg, v) && mount.setTargetDec(v) ? '1' : '0');
      break;
    case 't':                                // :StsDD*MM# latitude
      if (Astro::parseSexagesimal(arg, v) && fabs(v) <= 90.0) {
        settings.latitude = v;
        settings.save();
        send('1');
      } else {
        send('0');
      }
      break;
    case 'g':                                // :SgDDD*MM# longitude, west positive (0..360)
      if (Astro::parseSexagesimal(arg, v) && fabs(v) <= 360.0) {
        settings.longitude = Astro::norm180(-v);
        settings.save();
        send('1');
      } else {
        send('0');
      }
      break;
    case 'G':                                // :SGsHH.H# UTC offset
      if (Astro::parseSexagesimal(arg, v) && fabs(v) <= 14.0) {
        // Keep the local time unchanged; clients send the offset and local time in either order.
        Astro::setUtc(Astro::utcNow() + (v - _utcOffset) * 3600.0);
        _utcOffset = v;
        send('1');
      } else {
        send('0');
      }
      break;
    case 'L':                                // :SLHH:MM:SS# local time
      if (Astro::parseSexagesimal(arg, v) && v >= 0 && v < 24.0) {
        double day = floor(localNow() / 86400.0);
        Astro::setUtc(day * 86400.0 + v * 3600.0 + _utcOffset * 3600.0);
        send('1');
      } else {
        send('0');
      }
      break;
    case 'C': {                              // :SCMM/DD/YY# local date
      int mo, da, yr;
      if (sscanf(arg, "%d/%d/%d", &mo, &da, &yr) == 3 && mo >= 1 && mo <= 12 && da >= 1 && da <= 31) {
        if (yr < 100) yr += 2000;
        double tod = fmod(localNow(), 86400.0);
        if (tod < 0) tod += 86400.0;
        Astro::setUtc((double)Astro::daysFromCivil(yr, mo, da) * 86400.0 + tod + _utcOffset * 3600.0);
        send("1Updating Planetary Data#                              #");
      } else {
        send('0');
      }
      break;
    }
    default:
      break;
  }
}
