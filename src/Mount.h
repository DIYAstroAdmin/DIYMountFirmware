// German equatorial mount model and motion control.
//
// Axis convention (mechanical angles, degrees):
//   axis1 (RA/hour-angle axis): 0 = counterweight down, positive = the direction of
//                               sidereal tracking (hour angle increasing).
//   axis2 (Dec axis):           0 = telescope pointing at the pole.
//                               axis2 >= 0 -> HA = axis1 + 90  (scope east of pier, looking west)
//                               axis2 <  0 -> HA = axis1 - 90  (scope west of pier, looking east)
//                               |Dec| = 90 - |axis2|
// Home position is axis1 = 0, axis2 = 0. The park position is saved in settings (home by default).
//
// All public methods are thread safe; motion is serviced by an internal task.
#pragma once
#include <Arduino.h>
#include "Axis.h"
#include "Settings.h"

enum class MountState { Idle, Slewing, Homing };

enum MountError {
  M_OK = 0,
  M_PARKED,
  M_BELOW_HORIZON,
  M_ABOVE_LIMIT,
  M_LIMITS,
  M_BUSY,
  M_NO_TARGET,
  M_RANGE,
};

// Same numbering as ASCOM GuideDirections.
enum MountDir { DIR_NORTH = 0, DIR_SOUTH = 1, DIR_EAST = 2, DIR_WEST = 3 };

// Same numbering as ASCOM DriveRates.
enum TrackRate { TRACK_SIDEREAL = 0, TRACK_LUNAR = 1, TRACK_SOLAR = 2 };

// Same numbering as ASCOM PierSide.
enum PierSide { PIER_EAST = 0, PIER_WEST = 1 };

struct MountStatus {
  double ra;        // hours
  double dec;       // deg
  double ha;        // deg, -180..180
  double lst;       // hours
  double alt, az;   // deg
  double axis1, axis2;
  int pierSide;
  MountState state;
  bool tracking;
  bool slewing;     // goto, park, homing or manual move in progress
  bool parking;
  bool guiding;
  bool atHome;
  bool atPark;
  bool homed;       // home switches have been found since power-up
  bool driverOk[2];
  bool homeSwitch[2];
  const char *message;
};

class Mount {
 public:
  void begin();
  void getStatus(MountStatus &st);

  MountError setTracking(bool on);
  void setTrackRate(int rate);
  int trackRate();

  bool setTargetRa(double hours);
  bool setTargetDec(double deg);
  bool getTargetRa(double &hours);
  bool getTargetDec(double &deg);
  MountError gotoTarget();
  MountError syncTarget();
  MountError gotoEqu(double raHours, double decDeg);
  MountError syncEqu(double raHours, double decDeg);
  // Alt/az (deg, azimuth from north through east). The target is fixed relative to the
  // ground: the slew ends there without changing the tracking state.
  MountError gotoAltAz(double altDeg, double azDeg);
  MountError syncAltAz(double altDeg, double azDeg);
  int destinationSideOfPier(double raHours, double decDeg);   // -1 if unreachable

  // Manual motion. axis 0: positive = RA increasing (east); axis 1: positive = north.
  // timeoutMs > 0 stops the move automatically unless it is refreshed.
  MountError moveAxis(int axis, double degPerSec, uint32_t timeoutMs = 0);
  MountError moveDir(int dir, double degPerSec, uint32_t timeoutMs = 0);
  void stopAxis(int axis);

  MountError pulseGuide(int dir, uint32_t ms);
  void abort();                 // stop everything except tracking

  MountError findHome();        // seek the home switches, then go to the home position
  MountError park();            // slew to the park position and stop tracking
  void unpark();
  MountError setParkHere();     // save the current position as the park position

  // Axis configuration. Microsteps, gear ratio and direction in 'settings' take effect at
  // the next restart; restartNeeded() says whether the saved values differ from the running ones.
  double maxRate(int axis);     // deg/s
  bool restartNeeded();
  void applyCurrents();         // apply the motor currents from 'settings' now

  static const char *errorText(MountError e);

 private:
  enum SlewKind { SLEW_GOTO, SLEW_ALTAZ, SLEW_PARK, SLEW_HOME };
  enum HomePhase { HP_CLEAR, HP_SEEK, HP_BACKOFF, HP_APPROACH, HP_DONE };

  struct HomeAxis {
    Axis *axis;
    uint8_t pin;
    int dir;
    double switchAngle;
    double maxTravel;
    HomePhase phase;
    double startAngle;
    bool sw;            // debounced switch state
    uint8_t count;
  };

  static void taskEntry(void *arg);
  void tick();
  void tickIdle();
  void tickSlew();
  void tickHoming();
  void readSwitches();
  void updateLed();

  void axesToHaDec(double a1, double a2, double &ha, double &dec, int &side);
  void axesForSide(double ha, double dec, int side, double &a1, double &a2);
  bool solve(double ha, double dec, double &a1, double &a2, int &side);
  double currentHa(double raHours);
  double trackDegPerSec();
  double decDirection();        // sign of d(axis2)/d(dec) at the current position
  void startSlew(double a1, double a2, SlewKind kind);
  MountError gotoAllowed();     // M_OK if a new goto may start now
  MountError syncHaDec(double ha, double dec, double &a1, double &a2);
  void clearManual();

  SemaphoreHandle_t _mutex = nullptr;
  FastAccelStepperEngine _engine;
  Axis *_ra = nullptr;
  Axis *_dec = nullptr;
  HomeAxis _home[2];
  AxisSettings _active[2];           // axis settings the motors were started with

  MountState _state = MountState::Idle;
  SlewKind _slewKind = SLEW_GOTO;
  bool _tracking = false;
  int _trackRate = TRACK_SIDEREAL;
  bool _atPark = false;
  bool _saveParked = false;          // park finished: record it in settings outside the lock
  bool _homed = false;
  const char *_message = "";
  bool _fatal = false;               // motor start-up failed; the status LED stays off

  double _manual[2] = {0, 0};        // mechanical deg/s
  int64_t _manualEnd[2] = {0, 0};    // 0 = no timeout
  bool _manualNorth = false;         // logical direction of the current Dec manual move
  double _guide[2] = {0, 0};         // mechanical deg/s
  int64_t _guideEnd[2] = {0, 0};     // 0 = not guiding

  double _targetRa = 0, _targetDec = 0;
  bool _targetRaSet = false, _targetDecSet = false;

  double _gotoRa = 0, _gotoDec = 0;
  int _gotoSide = PIER_EAST;
  int _gotoIter = 0;

  friend struct MountLock;
};

extern Mount mount;
