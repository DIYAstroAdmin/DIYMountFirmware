#include "Axis.h"
#include "Config.h"

bool Axis::begin(FastAccelStepperEngine &engine, Stream &tmcSerial) {
  // Driver configuration over UART.
  _driver = new TMC2209Stepper(&tmcSerial, TMC_R_SENSE, _p.tmcAddress);
  _driver->begin();
  _driver->pdn_disable(true);        // use the UART, not the PDN pin
  _driver->mstep_reg_select(true);   // microstepping from the register, not the MS pins
  _driver->I_scale_analog(false);
  _driver->toff(4);
  _driver->rms_current(_p.runCurrentMa, HOLD_CURRENT_FRACTION);
  _driver->microsteps(_p.microsteps);
  _driver->intpol(true);             // interpolate to 256 microsteps
  _driver->en_spreadCycle(!TMC_STEALTHCHOP);
  _driver->pwm_autoscale(true);
  _driverOk = (_driver->test_connection() == 0);

  // Step generator.
  _stepper = engine.stepperConnectToPin(_p.stepPin);
  if (!_stepper) return false;
  _stepper->setDirectionPin(_p.dirPin, !_p.invertDir);
  _stepper->setAcceleration((int32_t)(SLEW_ACCELERATION * _p.stepsPerDeg));
  return true;
}

double Axis::maxRate() const {
  return min((double)MAX_SLEW_RATE, MAX_STEP_RATE / _p.stepsPerDeg);
}

void Axis::setCurrent(uint16_t ma) {
  if (!_driver || ma == _p.runCurrentMa) return;
  _p.runCurrentMa = ma;
  _driver->rms_current(ma, HOLD_CURRENT_FRACTION);
}

double Axis::angle() const {
  if (!_stepper) return 0;
  return ((double)_stepper->getCurrentPosition() - _zeroSteps) / _p.stepsPerDeg;
}

void Axis::setAngle(double deg) {
  if (!_stepper) return;
  _zeroSteps = (double)_stepper->getCurrentPosition() - deg * _p.stepsPerDeg;
}

void Axis::applySpeed(double sps) {
  // Period in timer ticks is much finer than milli-Hz at tracking speeds.
  if (sps < 2000.0) {
    _stepper->setSpeedInTicks((uint32_t)((double)TICKS_PER_S / sps + 0.5));
  } else {
    _stepper->setSpeedInMilliHz((uint32_t)(sps * 1000.0 + 0.5));
  }
}

void Axis::setVelocity(double degPerSec) {
  if (!_stepper) return;
  degPerSec = constrain(degPerSec, -maxRate(), maxRate());
  if (degPerSec == _lastVelocity) return;
  _lastVelocity = degPerSec;

  double sps = fabs(degPerSec) * _p.stepsPerDeg;
  if (sps < 0.01) {
    _stepper->stopMove();
    return;
  }
  applySpeed(sps);
  if (degPerSec > 0) {
    _stepper->runForward();
  } else {
    _stepper->runBackward();
  }
  _stepper->applySpeedAcceleration();
}

void Axis::moveTo(double deg, double maxDegPerSec) {
  if (!_stepper) return;
  _lastVelocity = NAN;
  maxDegPerSec = constrain(maxDegPerSec, 0.01, maxRate());
  applySpeed(maxDegPerSec * _p.stepsPerDeg);
  _stepper->moveTo((int32_t)lround(deg * _p.stepsPerDeg + _zeroSteps));
}

void Axis::stop() {
  if (!_stepper) return;
  _lastVelocity = NAN;
  _stepper->stopMove();
}

bool Axis::isRunning() const { return _stepper && _stepper->isRunning(); }
