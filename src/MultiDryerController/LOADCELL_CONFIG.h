// LOADCELL_CONFIG.h
// Multi Dryer Controller — HX711 load cell (weight in kg).
//
// Ported from references/loadcell/FishDryer/LOADCELL_CONFIG.h.
// Differences from the Nano baseline:
//   - Calibration factor is persisted in ESP32 NVS via Preferences
//     (the Nano used EEPROM; NVS is the native, wear-leveled option on ESP32).
//   - No auto-tare on boot: the HMI sends a TARE command (boot screen →
//     dashboard transition) and the user can TARE from diagnostics, exactly
//     like the FishDryer flow. This avoids zeroing the scale unexpectedly.
//
// Requires the HX711 Arduino library (bogde/HX711) — same as the baseline.

#ifndef LOADCELL_CONFIG_H
#define LOADCELL_CONFIG_H

#include <math.h>
#include <HX711.h>
#include <Preferences.h>
#include "PINS_CONFIG.h"

// Default factor — used only when no valid value is saved in NVS.
// After the first CALIBRATE:<kg> command the computed factor is stored
// and restored automatically on boot.
#define LOADCELL_CALIBRATION_FACTOR  -10697.956054f

// Number of samples averaged per reading.
// 3 keeps each read to ~300 ms at the HX711's default 10 SPS so the 1 Hz
// ESP-NOW status send doesn't block the loop for a full second.
// (Tie the HX711 RATE pin to 3.3 V for 80 SPS if smoother readings are needed.)
#define LOADCELL_SAMPLES  3

// NVS namespace/key for the calibration factor
#define LOADCELL_PREFS_NS  "loadcell"
#define LOADCELL_PREFS_KEY "factor"

HX711 scale;

void initLoadCell() {
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

    // Give HX711 time to stabilize after power-up (critical!)
    delay(500);

    // Wait for sensor to be ready before any operations
    uint32_t startWait = millis();
    while (!scale.is_ready() && (millis() - startWait) < 3000UL) {
        delay(10);
    }

    if (!scale.is_ready()) {
        Serial.println(F("WARNING: HX711 not responding! Check wiring (DOUT=35, SCK=32)."));
        scale.set_scale(LOADCELL_CALIBRATION_FACTOR);
        return;
    }

    // Restore calibration factor from NVS if a valid save exists
    Preferences prefs;
    prefs.begin(LOADCELL_PREFS_NS, true);   // read-only
    float savedFactor = prefs.getFloat(LOADCELL_PREFS_KEY, 0.0f);
    prefs.end();

    if (savedFactor > 0.0f && savedFactor < 999999.0f && isfinite(savedFactor)) {
        scale.set_scale(savedFactor);
        Serial.printf("Load cell: restored calibration factor %.6f\n", savedFactor);
    } else {
        scale.set_scale(LOADCELL_CALIBRATION_FACTOR);
        Serial.println(F("Load cell: no valid calibration found. Send TARE then CALIBRATE:<kg>."));
    }

    Serial.println(F("Load cell ready (kg). Waiting for TARE command from HMI."));
}

// Zero the scale (call with nothing on it)
void tareLoadCell() {
    if (!scale.is_ready()) { Serial.println(F("HX711 not ready!")); return; }
    scale.tare();
    Serial.println(F("Load cell tared. Place known weight, then send CALIBRATE:<kg>."));
}

// Run calibration against a known weight in KG.
// Step 1: TARE (empty scale)   Step 2: place known weight
// Step 3: CALIBRATE:<kg>       → factor computed and saved to NVS.
void calibrateLoadCell(float known_kg) {
    if (!scale.is_ready()) { Serial.println(F("HX711 not ready!")); return; }
    if (!(known_kg > 0.0f)) { Serial.println(F("CALIBRATE: invalid weight (must be > 0 kg)")); return; }

    scale.calibrate_scale(known_kg, LOADCELL_SAMPLES);
    float factor = scale.get_scale();

    // Persist to NVS so it survives power cycles
    Preferences prefs;
    prefs.begin(LOADCELL_PREFS_NS, false);  // read-write
    prefs.putFloat(LOADCELL_PREFS_KEY, factor);
    prefs.end();

    Serial.printf("--- Calibration complete ---\nFactor %.6f saved to NVS.\n", factor);
}

// Returns weight in KG. Returns 0 if sensor not ready.
// (Read at 1 Hz by the state machine — the "not ready" warning prints only on
// the ready→not-ready transition to avoid serial spam.)
float readLoadCell() {
    static bool lastReady = true;
    if (!scale.is_ready()) {
        if (lastReady) Serial.println(F("HX711 not ready!"));
        lastReady = false;
        return 0.0f;
    }
    lastReady = true;
    float kg = scale.get_units(LOADCELL_SAMPLES);
    if (kg < 0) kg = 0.0f;   // clamp negative noise
    return kg;
}

#endif // LOADCELL_CONFIG_H
