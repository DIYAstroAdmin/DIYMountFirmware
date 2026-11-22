#include "Mount.h"
#include "Astro.h"
#include "Config.h"
#include "Log.h"
#include "Settings.h"

Mount mount;

static const double SIDEREAL_DEG_S = 360.0 / 86164.0905;   // 15.041 arcsec/s
static const double LUNAR_DEG_S = 14.685 / 3600.0;
static const double SOLAR_DEG_S = 15.0 / 3600.0;
static const double AXIS2_LIMIT = 180.0;
static const double HOME_TOLERANCE = 0.01;   // deg

struct MountLock {
  explicit MountLock(Mount &m) : _m(m) { xSemaphoreTakeRecursive(_m._mutex, portMAX_DELAY); }
  ~MountLock() { xSemaphoreGiveRecursive(_m._mutex); }
  Mount &_m;
};

const char *Mount::errorText(MountError e) {
  switch (e) {
    case M_OK: return "";
    case M_PARKED: return "Mount is parked";
    case M_BELOW_HORIZON: return "Target is below the horizon limit";
    case M_ABOVE_LIMIT: return "Target is above the overhead limit";
    case M_LIMITS: return "Target is outside the mount limits";
    case M_BUSY: return "Mount is busy";
    case M_NO_TARGET: return "Target not set";
    case M_RANGE: return "Value out of range";
  }
  return "Error";
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void Mount::begin() {
  _mutex = xSemaphoreCreateRecursiveMutex();

  pinMode(PIN_MOTOR_ENABLE, OUTPUT);
  digitalWrite(PIN_MOTOR_ENABLE, HIGH);   // disabled while configuring
  pinMode(PIN_RA_HOME, INPUT);
  pinMode(PIN_DEC_HOME, INPUT);

  Serial1.begin(TMC_BAUD, SERIAL_8N1, PIN_TMC_RX, PIN_TMC_TX);
  _engine.init(1);   // step generator task on core 1, away from WiFi

  // Microsteps, gear ratio and direction are fixed from here until the next restart.
  for (int i = 0; i < 2; i++) _active[i] = settings.axis[i];
  const AxisSettings &ra = _active[0], &dec = _active[1];
  _ra = new Axis({PIN_RA_STEP, PIN_RA_DIR, ra.invert, Settings::stepsPerDeg(0, ra), RA_TMC_ADDRESS,
                  ra.currentMa, ra.microsteps});
  _dec = new Axis({PIN_DEC_STEP, PIN_DEC_DIR, dec.invert, Settings::stepsPerDeg(1, dec), DEC_TMC_ADDRESS,
                   dec.currentMa, dec.microsteps});
  if (!_ra->begin(_engine, Serial1)) {
    LOGE("RA axis: step generator could not be started");
    _fatal = true;
  }
  if (!_dec->begin(_engine, Serial1)) {
    LOGE("Dec axis: step generator could not be started");
    _fatal = true;
  }
  if (_ra->driverOk()) LOGI("RA driver ok");
  else {
    LOGE("RA driver not responding");
    _fatal = true;
  }
  if (_dec->driverOk()) LOGI("Dec driver ok");
  else {
    LOGE("Dec driver not responding");
    _fatal = true;
  }

  Axis *axes[2] = {_ra, _dec};
  const char *names[2] = {"RA", "Dec"};
  for (int i = 0; i < 2; i++) {
    LOGI("%s axis: %u microsteps, gear %.2f:1, %.1f steps/deg, %u mA%s", names[i], _active[i].microsteps,
         _active[i].gearRatio, axes[i]->stepsPerDeg(), _active[i].currentMa, _active[i].invert ? ", inverted" : "");
    if (axes[i]->maxRate() < MAX_SLEW_RATE)
      LOGW("%s axis: speed limited to %.2f deg/s by the step rate (fewer microsteps would allow more)",
           names[i], axes[i]->maxRate());
  }

  // A mount that was parked when it was switched off is still at the park position.
  if (settings.parked) {
    _ra->setAngle(settings.parkAxis1);
    _dec->setAngle(settings.parkAxis2);
    _atPark = true;
    LOGI("Starting parked");
  }

  _home[0] = {_ra, PIN_RA_HOME, 1, 0, RA_HOME_MAX_TRAVEL, HP_DONE, 0, false, 0};
  _home[1] = {_dec, PIN_DEC_HOME, 1, 0, DEC_HOME_MAX_TRAVEL, HP_DONE, 0, false, 0};

  digitalWrite(PIN_MOTOR_ENABLE, LOW);    // motors on, holding position

  // Stack leaves room for formatting log messages (printf with doubles) from the task.
  xTaskCreatePinnedToCore(taskEntry, "mount", 6144, this, 3, nullptr, 1);
}

void Mount::taskEntry(void *arg) {
  Mount *m = static_cast<Mount *>(arg);
  for (;;) {
    bool saveParked;
    {
      MountLock lock(*m);
      m->tick();
      m->updateLed();
      saveParked = m->_saveParked;
      m->_saveParked = false;
    }
    if (saveParked) {               // written outside the mount lock: writing flash is slow
      SettingsLock slock;
      bool parked;
      {
        MountLock lock(*m);
        parked = m->_atPark;          // an unpark may have arrived meanwhile
      }
      settings.parked = parked;
      settings.save();
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// Status LED: off after a start-up failure or when parked, 2 flashes/s while slewing, homing
// or moving manually, 1 flash/s while tracking, steady when idle. (Off before the mount task starts.)
void Mount::updateLed() {
#if PIN_STATUS_LED >= 0
  bool on;
  uint32_t t = millis();
  if (_fatal || _atPark) on = false;
  else if (_state != MountState::Idle || _manual[0] != 0 || _manual[1] != 0) on = (t % 500) < 250;
  else if (_tracking) on = (t % 1000) < 500;
  else on = true;
  digitalWrite(PIN_STATUS_LED, on ? STATUS_LED_ON : !STATUS_LED_ON);
#endif
}

// ---------------------------------------------------------------------------
// Pointing model
// ---------------------------------------------------------------------------
void Mount::axesToHaDec(double a1, double a2, double &ha, double &dec, int &side) {
  if (a2 >= 0) {
    ha = a1 + 90.0;
    side = PIER_EAST;
  } else {
    ha = a1 - 90.0;
    side = PIER_WEST;
  }
  ha = Astro::norm180(ha);
  dec = 90.0 - fabs(a2);
  if (settings.latitude < 0) dec = -dec;
}

void Mount::axesForSide(double ha, double dec, int side, double &a1, double &a2) {
  double poleDist = 90.0 - (settings.latitude < 0 ? -dec : dec);   // 0..180
  if (side == PIER_EAST) {
    a1 = Astro::norm180(ha - 90.0);
    a2 = poleDist;
  } else {
    a1 = Astro::norm180(ha + 90.0);
    a2 = -poleDist;
  }
}

// Picks the pier side for a target. Prefers the counterweight-down solution;
// returns false if neither side is within the axis1 limits.
bool Mount::solve(double ha, double dec, double &a1, double &a2, int &side) {
  double limit = 90.0 + settings.meridianLimitDeg();
  double e1, e2, w1, w2;
  axesForSide(ha, dec, PIER_EAST, e1, e2);
  axesForSide(ha, dec, PIER_WEST, w1, w2);
  bool eOk = fabs(e1) <= limit;
  bool wOk = fabs(w1) <= limit;
  if (!eOk && !wOk) return false;
  bool useEast;
  if (eOk && wOk) {
    // Smaller |axis1| keeps the counterweight lowest; on a tie take the one with
    // the most tracking time left (smaller axis1).
    if (fabs(fabs(e1) - fabs(w1)) < 1e-6) {
      useEast = e1 <= w1;
    } else {
      useEast = fabs(e1) < fabs(w1);
    }
  } else {
    useEast = eOk;
  }
  if (useEast) {
    a1 = e1; a2 = e2; side = PIER_EAST;
  } else {
    a1 = w1; a2 = w2; side = PIER_WEST;
  }
  return true;
}

double Mount::currentHa(double raHours) {
  return Astro::norm180((Astro::lstHours(settings.longitude) - raHours) * 15.0);
}

double Mount::trackDegPerSec() {
  switch (_trackRate) {
    case TRACK_LUNAR: return LUNAR_DEG_S;
    case TRACK_SOLAR: return SOLAR_DEG_S;
    default: return SIDEREAL_DEG_S;
  }
}

double Mount::decDirection() {
  double d = (_dec->angle() >= 0) ? -1.0 : 1.0;
  return settings.latitude < 0 ? -d : d;
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------
void Mount::getStatus(MountStatus &st) {
  MountLock lock(*this);
  st.axis1 = _ra->angle();
  st.axis2 = _dec->angle();
  axesToHaDec(st.axis1, st.axis2, st.ha, st.dec, st.pierSide);
  st.lst = Astro::lstHours(settings.longitude);
  st.ra = Astro::normHours(st.lst - st.ha / 15.0);
  Astro::haDecToAltAz(st.ha, st.dec, settings.latitude, st.alt, st.az);
  st.state = _state;
  st.tracking = _tracking;
  st.slewing = _state != MountState::Idle || _manual[0] != 0 || _manual[1] != 0;
  st.parking = _state == MountState::Slewing && _slewKind == SLEW_PARK;
  st.guiding = _guideEnd[0] != 0 || _guideEnd[1] != 0;
  st.atPark = _atPark;
  st.atHome = _state == MountState::Idle && !_tracking && fabs(st.axis1) < HOME_TOLERANCE &&
              fabs(st.axis2) < HOME_TOLERANCE;
  st.homed = _homed;
  st.driverOk[0] = _ra->driverOk();
  st.driverOk[1] = _dec->driverOk();
  st.homeSwitch[0] = _home[0].sw;
  st.homeSwitch[1] = _home[1].sw;
  st.message = _message;
}

// ---------------------------------------------------------------------------
// Tracking
// ---------------------------------------------------------------------------
MountError Mount::setTracking(bool on) {
  MountLock lock(*this);
  if (on) {
    if (_atPark) return M_PARKED;
    if (_state == MountState::Homing) return M_BUSY;
    if (_state == MountState::Slewing && _slewKind != SLEW_GOTO && _slewKind != SLEW_ALTAZ) return M_BUSY;
    _message = "";
  }
  _tracking = on;
  return M_OK;
}

void Mount::setTrackRate(int rate) {
  MountLock lock(*this);
  if (rate >= TRACK_SIDEREAL && rate <= TRACK_SOLAR) _trackRate = rate;
}

int Mount::trackRate() {
  MountLock lock(*this);
  return _trackRate;
}

// ---------------------------------------------------------------------------
// Targets, goto and sync
// ---------------------------------------------------------------------------
bool Mount::setTargetRa(double hours) {
  if (!(hours >= 0.0 && hours < 24.0)) return false;
  MountLock lock(*this);
  _targetRa = hours;
  _targetRaSet = true;
  return true;
}

bool Mount::setTargetDec(double deg) {
  if (!(deg >= -90.0 && deg <= 90.0)) return false;
  MountLock lock(*this);
  _targetDec = deg;
  _targetDecSet = true;
  return true;
}

bool Mount::getTargetRa(double &hours) {
  MountLock lock(*this);
  hours = _targetRa;
  return _targetRaSet;
}

bool Mount::getTargetDec(double &deg) {
  MountLock lock(*this);
  deg = _targetDec;
  return _targetDecSet;
}

MountError Mount::gotoTarget() {
  MountLock lock(*this);
  if (!_targetRaSet || !_targetDecSet) return M_NO_TARGET;
  return gotoEqu(_targetRa, _targetDec);
}

MountError Mount::syncTarget() {
  MountLock lock(*this);
  if (!_targetRaSet || !_targetDecSet) return M_NO_TARGET;
  return syncEqu(_targetRa, _targetDec);
}

void Mount::clearManual() {
  for (int i = 0; i < 2; i++) {
    _manual[i] = 0;
    _manualEnd[i] = 0;
    _guide[i] = 0;
    _guideEnd[i] = 0;
  }
}

void Mount::startSlew(double a1, double a2, SlewKind kind) {
  clearManual();
  _slewKind = kind;
  _gotoIter = 0;
  _state = MountState::Slewing;
  _message = "";
  _ra->moveTo(a1, settings.slewRate);
  _dec->moveTo(a2, settings.slewRate);
}

// A goto may replace a goto in progress, but not a park or home.
MountError Mount::gotoAllowed() {
  if (_atPark) return M_PARKED;
  if (_state == MountState::Homing) return M_BUSY;
  if (_state == MountState::Slewing && _slewKind != SLEW_GOTO && _slewKind != SLEW_ALTAZ) return M_BUSY;
  return M_OK;
}

MountError Mount::gotoEqu(double raHours, double decDeg) {
  if (!(raHours >= 0.0 && raHours < 24.0) || !(decDeg >= -90.0 && decDeg <= 90.0)) return M_RANGE;
  MountLock lock(*this);
  MountError e = gotoAllowed();
  if (e != M_OK) return e;

  double ha = currentHa(raHours);
  double alt, az;
  Astro::haDecToAltAz(ha, decDeg, settings.latitude, alt, az);
  if (alt < settings.minGotoAlt) return M_BELOW_HORIZON;
  if (alt > settings.maxGotoAlt) return M_ABOVE_LIMIT;

  double a1, a2;
  int side;
  if (!solve(ha, decDeg, a1, a2, side)) return M_LIMITS;

  _gotoRa = raHours;
  _gotoDec = decDeg;
  _gotoSide = side;
  LOGI("Goto RA %.4f h, Dec %.3f deg (pier %s, axes %.2f, %.2f)", raHours, decDeg,
       side == PIER_EAST ? "East" : "West", a1, a2);
  startSlew(a1, a2, SLEW_GOTO);
  return M_OK;
}

MountError Mount::gotoAltAz(double altDeg, double azDeg) {
  if (!(altDeg >= -90.0 && altDeg <= 90.0) || !(azDeg >= 0.0 && azDeg < 360.0)) return M_RANGE;
  MountLock lock(*this);
  MountError e = gotoAllowed();
  if (e != M_OK) return e;
  if (altDeg < settings.minGotoAlt) return M_BELOW_HORIZON;
  if (altDeg > settings.maxGotoAlt) return M_ABOVE_LIMIT;

  double ha, dec;
  Astro::altAzToHaDec(altDeg, azDeg, settings.latitude, ha, dec);
  double a1, a2;
  int side;
  if (!solve(ha, dec, a1, a2, side)) return M_LIMITS;

  LOGI("Goto Alt %.3f, Az %.3f deg (HA %.4f h, Dec %.3f deg, pier %s, axes %.2f, %.2f)", altDeg, azDeg,
       ha / 15.0, dec, side == PIER_EAST ? "East" : "West", a1, a2);
  startSlew(a1, a2, SLEW_ALTAZ);
  return M_OK;
}

MountError Mount::syncEqu(double raHours, double decDeg) {
  if (!(raHours >= 0.0 && raHours < 24.0) || !(decDeg >= -90.0 && decDeg <= 90.0)) return M_RANGE;
  MountLock lock(*this);
  double a1, a2;
  MountError e = syncHaDec(currentHa(raHours), decDeg, a1, a2);
  if (e == M_OK) LOGI("Sync to RA %.4f h, Dec %.3f deg (axes %.2f, %.2f)", raHours, decDeg, a1, a2);
  return e;
}

MountError Mount::syncAltAz(double altDeg, double azDeg) {
  if (!(altDeg >= -90.0 && altDeg <= 90.0) || !(azDeg >= 0.0 && azDeg < 360.0)) return M_RANGE;
  MountLock lock(*this);
  double ha, dec, a1, a2;
  Astro::altAzToHaDec(altDeg, azDeg, settings.latitude, ha, dec);
  MountError e = syncHaDec(ha, dec, a1, a2);
  if (e == M_OK) LOGI("Sync to Alt %.3f, Az %.3f deg (axes %.2f, %.2f)", altDeg, azDeg, a1, a2);
  return e;
}

// Declares the mount to be pointing at ha/dec; a1, a2 receive the new axis angles.
MountError Mount::syncHaDec(double ha, double decDeg, double &a1, double &a2) {
  if (_atPark) return M_PARKED;
  if (_state != MountState::Idle) return M_BUSY;

  double limit = 90.0 + settings.meridianLimitDeg();
  int side;
  double cur2 = _dec->angle();
  bool done = false;
  if (fabs(cur2) > HOME_TOLERANCE) {
    // Keep the pier side the mount is physically on, if that is a legal position.
    side = cur2 >= 0 ? PIER_EAST : PIER_WEST;
    axesForSide(ha, decDeg, side, a1, a2);
    done = fabs(a1) <= limit;
  }
  if (!done && !solve(ha, decDeg, a1, a2, side)) return M_LIMITS;
  _ra->setAngle(a1);
  _dec->setAngle(a2);
  return M_OK;
}

int Mount::destinationSideOfPier(double raHours, double decDeg) {
  MountLock lock(*this);
  double a1, a2;
  int side;
  if (!solve(currentHa(raHours), decDeg, a1, a2, side)) return -1;
  return side;
}

// ---------------------------------------------------------------------------
// Manual moves and guiding
// ---------------------------------------------------------------------------
MountError Mount::moveAxis(int axis, double degPerSec, uint32_t timeoutMs) {
  if (axis < 0 || axis > 1) return M_RANGE;
  if (fabs(degPerSec) > (axis == 0 ? _ra : _dec)->maxRate()) return M_RANGE;
  MountLock lock(*this);
  if (_atPark) return M_PARKED;
  if (_state != MountState::Idle) return M_BUSY;
  double mech;
  if (axis == 0) {
    mech = -degPerSec;   // RA increasing = hour angle decreasing
  } else {
    // A held move is refreshed periodically. Keep it going the same mechanical way:
    // the sign of decDirection() flips if the axis passes through the pole.
    bool north = degPerSec > 0;
    if (degPerSec != 0 && _manual[1] != 0 && north == _manualNorth) {
      mech = (_manual[1] > 0 ? 1.0 : -1.0) * fabs(degPerSec);
    } else {
      mech = degPerSec * decDirection();
    }
    _manualNorth = north;
  }
  _manual[axis] = mech;
  _manualEnd[axis] = (timeoutMs && mech != 0) ? Astro::microsNow() + (int64_t)timeoutMs * 1000 : 0;
  _message = "";
  return M_OK;
}

MountError Mount::moveDir(int dir, double degPerSec, uint32_t timeoutMs) {
  degPerSec = fabs(degPerSec);
  switch (dir) {
    case DIR_NORTH: return moveAxis(1, degPerSec, timeoutMs);
    case DIR_SOUTH: return moveAxis(1, -degPerSec, timeoutMs);
    case DIR_EAST: return moveAxis(0, degPerSec, timeoutMs);
    case DIR_WEST: return moveAxis(0, -degPerSec, timeoutMs);
  }
  return M_RANGE;
}

void Mount::stopAxis(int axis) {
  if (axis < 0 || axis > 1) return;
  MountLock lock(*this);
  _manual[axis] = 0;
  _manualEnd[axis] = 0;
}

MountError Mount::pulseGuide(int dir, uint32_t ms) {
  if (dir < DIR_NORTH || dir > DIR_WEST) return M_RANGE;
  MountLock lock(*this);
  if (_atPark) return M_PARKED;
  if (_state != MountState::Idle) return M_BUSY;
  double rate = settings.guideRate * SIDEREAL_DEG_S;
  int axis;
  double mech;
  switch (dir) {
    case DIR_EAST: axis = 0; mech = -rate; break;
    case DIR_WEST: axis = 0; mech = rate; break;
    case DIR_NORTH: axis = 1; mech = rate * decDirection(); break;
    default: axis = 1; mech = -rate * decDirection(); break;
  }
  if (ms == 0) {
    _guide[axis] = 0;
    _guideEnd[axis] = 0;
  } else {
    _guide[axis] = mech;
    _guideEnd[axis] = Astro::microsNow() + (int64_t)ms * 1000;
  }
  return M_OK;
}

void Mount::abort() {
  MountLock lock(*this);
  clearManual();
  if (_state != MountState::Idle) {
    _ra->stop();
    _dec->stop();
    _state = MountState::Idle;
    _message = "Stopped";
    LOGW("Motion aborted");
  }
}

// ---------------------------------------------------------------------------
// Axis configuration
// ---------------------------------------------------------------------------
double Mount::maxRate(int axis) {
  return (axis == 0 ? _ra : _dec)->maxRate();
}

bool Mount::restartNeeded() {
  for (int i = 0; i < 2; i++) {
    const AxisSettings &s = settings.axis[i], &a = _active[i];
    if (s.microsteps != a.microsteps || s.gearRatio != a.gearRatio || s.invert != a.invert) return true;
  }
  return false;
}

void Mount::applyCurrents() {
  MountLock lock(*this);
  _ra->setCurrent(settings.axis[0].currentMa);
  _dec->setCurrent(settings.axis[1].currentMa);
}

// ---------------------------------------------------------------------------
// Home and park
// ---------------------------------------------------------------------------
MountError Mount::findHome() {
  MountLock lock(*this);
  if (_atPark) return M_PARKED;
  if (_state == MountState::Homing) return M_OK;
  clearManual();
  _tracking = false;
  _ra->stop();
  _dec->stop();
  for (int i = 0; i < 2; i++) {
    _home[i].dir = settings.axis[i].homeDir;
    _home[i].switchAngle = settings.axis[i].homeAngle;
    _home[i].phase = _home[i].sw ? HP_CLEAR : HP_SEEK;
    _home[i].startAngle = _home[i].axis->angle();
  }
  _state = MountState::Homing;
  _message = "";
  LOGI("Home search started");
  return M_OK;
}

MountError Mount::park() {
  MountLock lock(*this);
  if (_atPark) return M_OK;
  if (_state == MountState::Homing) return M_BUSY;
  _tracking = false;
  LOGI("Parking");
  startSlew(settings.parkAxis1, settings.parkAxis2, SLEW_PARK);
  return M_OK;
}

void Mount::unpark() {
  bool wasParked;
  {
    MountLock lock(*this);
    wasParked = _atPark;
    _atPark = false;
    _saveParked = false;
  }
  if (!wasParked) return;
  LOGI("Unparked");
  SettingsLock lock;
  settings.parked = false;
  settings.save();
}

MountError Mount::setParkHere() {
  double a1, a2;
  {
    MountLock lock(*this);
    if (_state != MountState::Idle || _manual[0] != 0 || _manual[1] != 0) return M_BUSY;
    a1 = _ra->angle();
    a2 = _dec->angle();
  }
  {
    // Saved outside the mount lock: writing flash is slow.
    SettingsLock lock;
    settings.parkAxis1 = a1;
    settings.parkAxis2 = a2;
    settings.save();
  }
  LOGI("Park position set to axis1 %.2f, axis2 %.2f deg", a1, a2);
  return M_OK;
}

// ---------------------------------------------------------------------------
// Motion task
// ---------------------------------------------------------------------------
void Mount::readSwitches() {
  for (int i = 0; i < 2; i++) {
    bool raw = digitalRead(_home[i].pin) == (HOME_SWITCH_ACTIVE_LOW ? LOW : HIGH);
    if (raw == _home[i].sw) {
      _home[i].count = 0;
    } else if (++_home[i].count >= 3) {
      _home[i].sw = raw;
      _home[i].count = 0;
    }
  }
}

void Mount::tick() {
  readSwitches();
  switch (_state) {
    case MountState::Idle: tickIdle(); break;
    case MountState::Slewing: tickSlew(); break;
    case MountState::Homing: tickHoming(); break;
  }
}

void Mount::tickIdle() {
  int64_t now = Astro::microsNow();
  for (int i = 0; i < 2; i++) {
    if (_guideEnd[i] && now >= _guideEnd[i]) {
      _guideEnd[i] = 0;
      _guide[i] = 0;
    }
    if (_manualEnd[i] && now >= _manualEnd[i]) {
      _manualEnd[i] = 0;
      _manual[i] = 0;
    }
  }

  double v1 = (_tracking ? trackDegPerSec() : 0.0) + _manual[0] + _guide[0];
  double v2 = _manual[1] + _guide[1];

  // Axis limits: never drive further out once a limit is reached.
  double limit1 = 90.0 + settings.meridianLimitDeg();
  double a1 = _ra->angle();
  if ((a1 >= limit1 && v1 > 0) || (a1 <= -limit1 && v1 < 0)) {
    if (_tracking) {
      _message = "Tracking stopped at the meridian limit";
      LOGW("Tracking stopped at the meridian limit (axis 1 at %.2f deg)", a1);
    }
    _tracking = false;
    _manual[0] = 0;
    _manualEnd[0] = 0;
    _guide[0] = 0;
    _guideEnd[0] = 0;
    v1 = 0;
  }
  double a2 = _dec->angle();
  if ((a2 >= AXIS2_LIMIT && v2 > 0) || (a2 <= -AXIS2_LIMIT && v2 < 0)) {
    _manual[1] = 0;
    _manualEnd[1] = 0;
    _guide[1] = 0;
    _guideEnd[1] = 0;
    v2 = 0;
  }

  _ra->setVelocity(v1);
  _dec->setVelocity(v2);
}

void Mount::tickSlew() {
  if (_ra->isRunning() || _dec->isRunning()) return;

  if (_slewKind == SLEW_GOTO && _gotoIter < 3) {
    // The target moved in hour angle while we were slewing: correct for it.
    double a1, a2;
    axesForSide(currentHa(_gotoRa), _gotoDec, _gotoSide, a1, a2);
    double tol1 = max(1.5 / _ra->stepsPerDeg(), 1.0 / 3600.0);
    double tol2 = max(1.5 / _dec->stepsPerDeg(), 1.0 / 3600.0);
    if (fabs(a1 - _ra->angle()) > tol1 || fabs(a2 - _dec->angle()) > tol2) {
      _gotoIter++;
      _ra->moveTo(a1, settings.slewRate);
      _dec->moveTo(a2, settings.slewRate);
      return;
    }
  }

  _state = MountState::Idle;
  switch (_slewKind) {
    case SLEW_GOTO:
      _tracking = true;
      LOGI("Goto complete, tracking");
      break;
    case SLEW_ALTAZ:                      // tracking left as it is (off, unless switched on meanwhile)
      LOGI("Alt/az goto complete");
      break;
    case SLEW_PARK:
      _tracking = false;
      _atPark = true;
      _saveParked = true;
      LOGI("Parked");
      break;
    case SLEW_HOME:
      _tracking = false;
      LOGI("At home");
      break;
  }
}

void Mount::tickHoming() {
  bool allDone = true;
  for (int i = 0; i < 2; i++) {
    HomeAxis &h = _home[i];
    Axis &ax = *h.axis;
    switch (h.phase) {
      case HP_CLEAR:      // started on the switch: move off it first
        ax.setVelocity(-h.dir * HOMING_FAST_RATE);
        if (!h.sw) h.phase = HP_SEEK;
        break;
      case HP_SEEK:
        ax.setVelocity(h.dir * HOMING_FAST_RATE);
        if (h.sw) h.phase = HP_BACKOFF;
        break;
      case HP_BACKOFF:
        ax.setVelocity(-h.dir * HOMING_SLOW_RATE * 5.0);
        if (!h.sw) h.phase = HP_APPROACH;
        break;
      case HP_APPROACH:
        ax.setVelocity(h.dir * HOMING_SLOW_RATE);
        if (h.sw) {
          ax.setAngle(h.switchAngle);
          ax.setVelocity(0);
          h.phase = HP_DONE;
        }
        break;
      case HP_DONE:
        break;
    }
    if (h.phase != HP_DONE) {
      allDone = false;
      if (fabs(ax.angle() - h.startAngle) > h.maxTravel) {
        _ra->stop();
        _dec->stop();
        _state = MountState::Idle;
        _message = i == 0 ? "Homing failed: RA switch not found" : "Homing failed: Dec switch not found";
        LOGE("%s", _message);
        return;
      }
    }
  }
  if (allDone && !_ra->isRunning() && !_dec->isRunning()) {
    _homed = true;
    LOGI("Home switches found, moving to home");
    startSlew(0, 0, SLEW_HOME);
  }
}
