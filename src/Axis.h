// One motor axis: TMC2209 driver setup plus step generation (FastAccelStepper).
// Angles are in degrees of the mount axis; the zero point can be moved without
// touching the step generator, so it is safe to re-zero while the motor is running.
#pragma once
#include <Arduino.h>
#include <FastAccelStepper.h>
#include <TMCStepper.h>

class Axis {
 public:
  struct Params {
    uint8_t stepPin;
    uint8_t dirPin;
    bool invertDir;
    double stepsPerDeg;
    uint8_t tmcAddress;
    uint16_t runCurrentMa;
    uint16_t microsteps;
  };

  explicit Axis(const Params &p) : _p(p) {}

  bool begin(FastAccelStepperEngine &engine, Stream &tmcSerial);

  double angle() const;                    // current axis angle, deg
  void setAngle(double deg);               // declare the current position to be 'deg'
  void setVelocity(double degPerSec);      // continuous motion, 0 = decelerate to a stop
  void moveTo(double deg, double maxDegPerSec);
  void stop();                             // decelerate to a stop
  bool isRunning() const;
  bool driverOk() const { return _driverOk; }
  double stepsPerDeg() const { return _p.stepsPerDeg; }
  double maxRate() const;                   // deg/s, MAX_SLEW_RATE or less if the step rate limits it
  void setCurrent(uint16_t ma);             // RMS run current, takes effect immediately

 private:
  void applySpeed(double stepsPerSec);

  Params _p;
  FastAccelStepper *_stepper = nullptr;
  TMC2209Stepper *_driver = nullptr;
  bool _driverOk = false;
  double _zeroSteps = 0;       // stepper position that corresponds to angle 0
  double _lastVelocity = NAN;  // last velocity command, NAN = unknown / position move
};
