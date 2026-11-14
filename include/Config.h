// MyMount project configuration.
// Everything that describes the hardware lives here and is fixed at compile time.
// Site, WiFi and rate settings have defaults here but can be changed from the web UI.
#pragma once

// ---------------------------------------------------------------------------
// Mechanics
// ---------------------------------------------------------------------------
// Total reduction = motor revolutions per one revolution of the axis
// (e.g. 3:1 belt stage x 144:1 worm = 432).

#define RA_MOTOR_STEP_ANGLE   0.9     // degrees per full step (1.8 or 0.9)
#define RA_MICROSTEPS         32      // 1,2,4,8,16,32,64,128,256
#define RA_GEAR_RATIO         100
#define RA_INVERT_DIR         false   // flip if the axis turns the wrong way (see README)

#define DEC_MOTOR_STEP_ANGLE  0.9
#define DEC_MICROSTEPS        128
#define DEC_GEAR_RATIO        100
#define DEC_INVERT_DIR        false

// ---------------------------------------------------------------------------
// TMC2209 drivers
// ---------------------------------------------------------------------------
#define RA_RUN_CURRENT_MA     500     // RMS motor current
#define DEC_RUN_CURRENT_MA    500
#define HOLD_CURRENT_FRACTION 0.5     // standstill current as a fraction of run current
#define TMC_R_SENSE           0.11f   // sense resistor on the E4
#define TMC_STEALTHCHOP       true    // false = spreadCycle (more torque at speed, noisier)

// ---------------------------------------------------------------------------
// Motion
// ---------------------------------------------------------------------------
#define MAX_SLEW_RATE         3.0     // deg/s, hard upper limit for goto and manual moves
#define SLEW_ACCELERATION     2.0     // deg/s^2
#define CENTER_RATE_SIDEREAL  16.0    // LX200 "centre" rate, multiples of sidereal
#define MIN_GOTO_ALTITUDE     0.0     // deg, gotos below this altitude are refused

// ---------------------------------------------------------------------------
// Homing (switches on the E4 X-MIN and Y-MIN connectors)
// ---------------------------------------------------------------------------
// The mount's home position is counterweight down, telescope pointing at the pole.
// *_HOME_SWITCH_ANGLE is where the axis is (degrees from that home position, in the
// axis' positive direction) at the moment its switch triggers.
#define HOME_SWITCH_ACTIVE_LOW   true
#define RA_HOME_SEARCH_DIR       1       // +1 or -1: direction to travel to find the switch
#define RA_HOME_SWITCH_ANGLE     0.0
#define RA_HOME_MAX_TRAVEL       200.0   // deg, give up if the switch is not found within this
#define DEC_HOME_SEARCH_DIR      1
#define DEC_HOME_SWITCH_ANGLE    0.0
#define DEC_HOME_MAX_TRAVEL      380.0
#define HOMING_FAST_RATE         2.0     // deg/s
#define HOMING_SLOW_RATE         0.1     // deg/s, final approach

// ---------------------------------------------------------------------------
// Runtime defaults (changeable from the web UI, stored in flash)
// ---------------------------------------------------------------------------
#define DEFAULT_WIFI_SSID        "Zen Internet - FRITZ!Box GF"  // empty = start own access point
#define DEFAULT_WIFI_PASSWORD    "09390315960648919448"
#define DEFAULT_HOSTNAME         "mymount"   // also the access point name
#define AP_PASSWORD              "mymount123"   // min 8 characters
#define DEFAULT_LATITUDE         51.5        // deg, north positive
#define DEFAULT_LONGITUDE        0.0         // deg, east positive
#define DEFAULT_ELEVATION        0.0         // m
#define DEFAULT_GUIDE_RATE       0.5         // multiples of sidereal
#define DEFAULT_SLEW_RATE        2.0         // deg/s
#define DEFAULT_MERIDIAN_LIMIT   5.0         // deg the mount may track/slew past the meridian

#define NTP_SERVER               "pool.ntp.org"   // used when connected to a WiFi network

// ---------------------------------------------------------------------------
// Serial (LX200) - the E4's USB port
// ---------------------------------------------------------------------------
#define LX200_BAUD               9600

// ---------------------------------------------------------------------------
// FYSETC E4 pins (RA = X driver, Dec = Y driver)
// ---------------------------------------------------------------------------
#define PIN_RA_STEP        27
#define PIN_RA_DIR         26
#define PIN_DEC_STEP       33
#define PIN_DEC_DIR        32
#define PIN_MOTOR_ENABLE   25    // shared by all four drivers, active low
#define PIN_RA_HOME        34    // X-MIN (input only, pulled up on the board)
#define PIN_DEC_HOME       35    // Y-MIN
#define PIN_TMC_RX         21
#define PIN_TMC_TX         22
#define TMC_BAUD           115200
#define RA_TMC_ADDRESS     1     // X driver UART address on the E4
#define DEC_TMC_ADDRESS    3     // Y driver UART address on the E4

// ---------------------------------------------------------------------------
// Derived values - do not edit
// ---------------------------------------------------------------------------
#define RA_STEPS_PER_DEG   ((360.0 / RA_MOTOR_STEP_ANGLE) * RA_MICROSTEPS * RA_GEAR_RATIO / 360.0)
#define DEC_STEPS_PER_DEG  ((360.0 / DEC_MOTOR_STEP_ANGLE) * DEC_MICROSTEPS * DEC_GEAR_RATIO / 360.0)

static_assert(MAX_SLEW_RATE * RA_STEPS_PER_DEG <= 200000.0,
              "RA: MAX_SLEW_RATE needs more than 200 kHz step rate - lower it or the microstepping");
static_assert(MAX_SLEW_RATE * DEC_STEPS_PER_DEG <= 200000.0,
              "Dec: MAX_SLEW_RATE needs more than 200 kHz step rate - lower it or the microstepping");

#define FIRMWARE_NAME      "MyMount"
#define FIRMWARE_VERSION   "1.0"
