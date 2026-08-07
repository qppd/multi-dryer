// PID_CONFIG.h
// Multi Dryer Controller — PID temperature control for the PTC heater,
// using SHT31 temperature feedback.
//
// Adapted from references/loadcell/FishDryer/PID_CONFIG.h:
//   - Same PID_v1 library and PD tuning style (KP/KI/KD).
//   - Same output model: 0..5000, output > 0 → heat, else vent.
//   - SSR mapping adapted to the Multi Dryer loads:
//       heating: SSR1 (PTC heater) + SSR3 (inlet fan)  ON,
//                SSR2 (exhaust outlet) + SSR4 (exhaust fan) OFF
//       venting: SSR1 + SSR3 OFF, SSR2 (outlet open) + SSR4 ON
//   - NEW safety guard (not in the baseline): if the SHT31 feedback is
//     unavailable, the heater is forced OFF and the unit vents instead —
//     a dead sensor must never cause full-power heating.
//
// Requires the Arduino PID library (br3ttb/PID_v1).

#ifndef PID_CONFIG_H
#define PID_CONFIG_H

#include <PID_v1.h>
#include "PINS_CONFIG.h"
#include "SHT31_CONFIG.h"

// ── Tuning parameters (starting point — tune on the real rig) ─────────────────
#define PID_KP  4.0
#define PID_KI  0.0
#define PID_KD  22.0

// Externals defined in MultiDryerController.ino
extern double TEMPERATURE_SETPOINT;
extern double PID_OUTPUT;

// PID_v1 wants double in/out/setpoint — CURRENT_TEMPERATURE is already double
PID pid(&CURRENT_TEMPERATURE, &PID_OUTPUT, &TEMPERATURE_SETPOINT, PID_KP, PID_KI, PID_KD, DIRECT);

// SSR driver implemented in MultiDryerController.ino
extern void operateSSR(int relayIndex, bool state);

// ── Init (call once in setup()) ───────────────────────────────────────────────
void initPID() {
    pid.SetOutputLimits(0, 5000);
    pid.SetSampleTime(SHT31_READ_INTERVAL_MS);   // match sensor refresh rate (2 s)
    pid.SetMode(MANUAL);                         // stay off until the HMI sends PID_START / START_DRYING
    PID_OUTPUT = 0;
}

// ── Control ───────────────────────────────────────────────────────────────────
void setPIDSetpoint(double setpoint) {
    TEMPERATURE_SETPOINT = setpoint;
    Serial.printf("[PID] setpoint %.1f C\n", setpoint);
}

void startPID() {
    pid.SetMode(AUTOMATIC);
    Serial.printf("[PID] started (setpoint %.1f C)\n", TEMPERATURE_SETPOINT);
}

void stopPID() {
    pid.SetMode(MANUAL);
    PID_OUTPUT = 0;
    operateSSR(1, false);
    operateSSR(2, false);
    operateSSR(3, false);
    operateSSR(4, false);
    Serial.println(F("[PID] stopped — all outputs OFF"));
}

// ── Compute + drive SSRs (call every loop(); PID_v1 throttles to sample time) ─
void pidCOMPUTE() {
    // Stopped (MANUAL) means truly all-off — never touch SSRs here, so
    // stopPID()'s all-off promise holds and manual HEATER_ON overrides survive.
    if (pid.GetMode() == MANUAL) return;

    // Safety guard: never heat without valid temperature feedback — vent instead.
    if (!sht31OK) {
        PID_OUTPUT = 0;         // keep status flags consistent with real SSR state
        operateSSR(1, false);   // heater OFF
        operateSSR(3, false);   // inlet fan OFF
        operateSSR(2, true);    // exhaust outlet OPEN
        operateSSR(4, true);    // exhaust fan ON — vent instead of heating
        return;
    }

    pid.Compute();

    if (PID_OUTPUT > 0) {
        // Heating: PTC + inlet fan circulate air; vents closed
        operateSSR(1, true);
        operateSSR(3, true);
        operateSSR(2, false);
        operateSSR(4, false);
    } else {
        // Venting: heater/inlet off, exhaust outlet open + exhaust fan on
        operateSSR(1, false);
        operateSSR(3, false);
        operateSSR(2, true);
        operateSSR(4, true);
    }
}

#endif // PID_CONFIG_H
