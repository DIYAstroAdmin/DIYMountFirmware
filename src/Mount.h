// German equatorial mount model and motion control.
//
// Axis convention (mechanical angles, degrees):
//   axis1 (RA/hour-angle axis): 0 = counterweight down, positive = the direction of
//                               sidereal tracking (hour angle increasing).
//   axis2 (Dec axis):           0 = telescope pointing at the pole.
//                               axis2 >= 0 -> HA = axis1 + 90  (scope east of pier, looking west)
//                               axis2 <  0 -> HA = axis1 - 90  (scope west of pier, looking east)
//                               |Dec| = 90 - |axis2|
// Home/park position is axis1 = 0, axis2 = 0.
//
// All public methods are thread safe; motion is serviced by an internal task.
#pragma once
#include <Arduino.h>
#include "Axis.h"

enum class MountState { Idle, Slewing, Homing };

enum MountError {
  M_OK = 0,
  M_PARKED,
  M_BELOW_HORIZON,
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
  int destinationSideOfPier(double raHours, double decDeg);   // -1 if unreachable

  // Manual motion. axis 0: positive = RA increasing (east); axis 1: positive = north.
  // timeoutMs > 0 stops the move automatically unless it is refreshed.
  MountError moveAxis(int axis, double degPerSec, uint32_t timeoutMs = 0);
  MountError moveDir(int dir, double degPerSec, uint32_t timeoutMs = 0);
  void stopAxis(int axis);

  MountError pulseGuide(int dir, uint32_t ms);
  void abort();                 // stop everything except tracking

  MountError findHome();        // seek the home switches, then go to the home position
  MountError park();            // slew to the home position and stop tracking
  void unpark();
  MountError setHomeHere();     // declare the current position to be the home position

  static const char *errorText(MountError e);

 private:
  enum SlewKind { SLEW_GOTO, SLEW_PARK, SLEW_HOME };
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

  void axesToHaDec(double a1, double a2, double &ha, double &dec, int &side);
  void axesForSide(double ha, double dec, int side, double &a1, double &a2);
  bool solve(double ha, double dec, double &a1, double &a2, int &side);
  double currentHa(double raHours);
  double trackDegPerSec();
  double decDirection();        // sign of d(axis2)/d(dec) at the current position
  void startSlew(double a1, double a2, SlewKind kind);
  void clearManual();

  SemaphoreHandle_t _mutex = nullptr;
  FastAccelStepperEngine _engine;
  Axis *_ra = nullptr;
  Axis *_dec = nullptr;
  HomeAxis _home[2];

  MountState _state = MountState::Idle;
  SlewKind _slewKind = SLEW_GOTO;
  bool _tracking = false;
  int _trackRate = TRACK_SIDEREAL;
  bool _atPark = false;
  bool _homed = false;
  const char *_message = "";

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
