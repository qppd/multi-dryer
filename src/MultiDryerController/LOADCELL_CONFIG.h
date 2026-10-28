// LOADCELL_CONFIG.h
// Multi Dryer Controller — HX711 load cell (weight in kg).
//
// Library: HX711_ADC by Olav Kallhovd (https://github.com/olkal/HX711_ADC).
// This is NOT bogde's / Rob Tillaart's "HX711" library — those also ship an
// HX711.h, so install only HX711_ADC (or the sketch may pick the wrong one).
//
// Wiring (HX711 -> ESP32 38-pin):
//   HX711 DT  -> GPIO33   (LOADCELL_DOUT_PIN)
//   HX711 SCK -> GPIO32   (LOADCELL_SCK_PIN)
//   HX711 VCC -> 3V3, HX711 GND -> GND
//
// Behaviour:
//   - updateLoadCell() must be called on every loop() pass; HX711_ADC reads
//     one conversion at a time and needs the frequent polling.
//   - No automatic tare on boot: the HMI sends TARE (or use the serial command)
//     so a controller reboot never destroys the drying session's zero point.
//   - Calibration factor is persisted in ESP32 NVS (Preferences) under
//     namespace "loadcell", key "factor", and restored on every boot.
//
// Bench serial commands (115200 baud, "Newline" line ending):
//   HELP                show this list
//   TARE                zero the scale (empty platform)
//   CALIBRATE:<kg>      calibrate with a known weight, e.g. CALIBRATE:20.5
//   READ                print one reading
//   STATUS              factor, stream state, readiness, current weight
//   FACTOR              print the active calibration factor
//   FACTOR:<value>      set the factor manually and save it
//   RESET_CAL           forget the stored calibration, back to default factor
//   STREAM:ON|OFF       enable/disable periodic weight output
//   INTERVAL:<ms>       periodic output period, e.g. INTERVAL:500
//
// NOTE: the serial TARE/CALIBRATE commands bypass the HMI's "session active"
// guard in espnow_link.h. Use them on the bench only, with no drying session
// running — taring mid-session would corrupt the water-loss reference.

#ifndef LOADCELL_CONFIG_H
#define LOADCELL_CONFIG_H

#include <Arduino.h>
#include <math.h>
#include <Preferences.h>
#include <HX711_ADC.h>
#include "PINS_CONFIG.h"

// ===========================================================
// Calibration factor — used only when no valid value exists in
// NVS. The value is counts per kg (HX711_ADC divides raw counts
// by it). After CALIBRATE:<kg> the computed factor is saved and
// restored automatically on boot.
// ===========================================================
#define LOADCELL_CALIBRATION_FACTOR  -10697.956054f

// Stabilizing time (ms) before the first conversions are accepted
#define LOADCELL_STABILIZING_MS      2000

// If no new sample arrived for this long, the HX711 is considered dead
#define LOADCELL_READY_TIMEOUT_MS    1000

// NVS namespace/key for the calibration factor.
// "factor_v2": the previous firmware used a different HX711 library whose
// saved factor had the opposite meaning (kg/count instead of counts/kg).
// A new key guarantees a stale value can never be applied as a huge weight.
#define LOADCELL_PREFS_NS  "loadcell"
#define LOADCELL_PREFS_KEY "factor_v2"
#define LOADCELL_PREFS_KEY_LEGACY "factor"

// HX711_ADC instance (pins come from PINS_CONFIG.h)
HX711_ADC loadCell(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

// Last filtered reading (kg) and millis() of the last new conversion
float    loadCellKg         = 0.0f;
uint32_t loadCellLastDataMs = 0;

// Defined below; initLoadCell() polls it while waiting for the first sample.
bool updateLoadCell();

// Serial weight stream (bench diagnostics)
bool     loadCellStreamEnabled  = true;
uint32_t loadCellPrintIntervalMs = 1000;
uint32_t loadCellLastPrintMs     = 0;
String   loadCellCmdLine;

// A calibration factor is valid if it is finite and non-zero. Negative
// factors are legal: they mean the load cell is wired with A+/A- reversed
// (raw counts run negative, weight still comes out positive).
static bool loadCellFactorValid(float factor) {
    return isfinite(factor) && fabsf(factor) > 1e-6f && fabsf(factor) < 1e12f;
}

// --- NVS helpers --------------------------------------------
void loadCellSaveFactor(float factor) {
    Preferences prefs;
    prefs.begin(LOADCELL_PREFS_NS, false);   // read-write
    prefs.putFloat(LOADCELL_PREFS_KEY, factor);
    prefs.remove(LOADCELL_PREFS_KEY_LEGACY); // drop any old-library value
    prefs.end();
}

float getCalibrationFactor() {
    return loadCell.getCalFactor();
}

// Apply a factor manually and persist it
void setCalibrationFactor(float factor) {
    if (!loadCellFactorValid(factor)) {
        Serial.println(F("Invalid factor (must be finite and non-zero)."));
        return;
    }
    loadCell.setCalFactor(factor);
    loadCellSaveFactor(factor);
}

// Forget the stored factor and go back to the default
void clearCalibration() {
    Preferences prefs;
    prefs.begin(LOADCELL_PREFS_NS, false);
    prefs.remove(LOADCELL_PREFS_KEY);
    prefs.remove(LOADCELL_PREFS_KEY_LEGACY);   // drop the old-library value too
    prefs.end();
    loadCell.setCalFactor(LOADCELL_CALIBRATION_FACTOR);
}

void loadCellRestoreFactor() {
    Preferences prefs;
    prefs.begin(LOADCELL_PREFS_NS, true);    // read-only
    float savedFactor = prefs.getFloat(LOADCELL_PREFS_KEY, 0.0f);
    prefs.end();

    if (loadCellFactorValid(savedFactor)) {
        loadCell.setCalFactor(savedFactor);
        Serial.printf("Load cell: restored calibration factor %.6f from NVS.\n", savedFactor);
        return;
    }

    loadCell.setCalFactor(LOADCELL_CALIBRATION_FACTOR);
    Serial.println(F("Load cell: no valid calibration found, using default factor."));
    Serial.println(F("  Send TARE then CALIBRATE:<kg> to calibrate."));
}

// --- Tare-offset bridge for DRYER_STATE.h --------------------
// The drying session saves the HX711 zero reference so it survives a reboot.
// HX711_ADC stores it as an offset-binary raw value (smoothed raw XOR 0x800000).
long loadCellGetOffset() {
    return loadCell.getTareOffset();
}

void loadCellSetOffset(long offset) {
    loadCell.setTareOffset(offset);
    loadCellKg = loadCell.getData();   // refresh the cache with the new zero
}

// --- Lifecycle ----------------------------------------------
void initLoadCell() {
    loadCell.begin();

    // begin() configures DOUT as a plain INPUT. A pull-up keeps the line HIGH
    // when the HX711 is unpowered, so floating noise is never mistaken for a
    // conversion (the HX711's push-pull output overrides it when powered).
    pinMode(LOADCELL_DOUT_PIN, INPUT_PULLUP);

    loadCellRestoreFactor();

    // HX711_ADC's start(t) waits against the *absolute* millis() value, so it
    // only behaves as intended when called at the very start of setup(). This
    // controller initializes ESP-NOW first (so the HMI's boot TARE is queued),
    // therefore use a relative settling loop here instead. It also fills the
    // moving-average dataset at least once before the first reading is used.
    Serial.printf("Load cell: stabilizing %lu ms... ",
                  (unsigned long)LOADCELL_STABILIZING_MS);

    const int      needed  = loadCell.getSamplesInUse() + 2;   // + ignore-high/low
    int            samples = 0;
    uint32_t       started = millis();
    const uint32_t hardStop = LOADCELL_STABILIZING_MS + 3000UL;

    while ((millis() - started) < LOADCELL_STABILIZING_MS || samples < needed) {
        if (loadCell.update()) {
            loadCellKg         = loadCell.getData();
            loadCellLastDataMs = millis();
            samples++;
        }
        if ((millis() - started) > hardStop) break;   // no HX711 -> don't hang boot
        delay(1);
    }
    Serial.println(F("done."));

    if (loadCellLastDataMs == 0) {
        loadCellKg = 0.0f;
        loadCellLastPrintMs = millis();
        Serial.println(F("WARNING: HX711 not responding! Check wiring (DOUT=33, SCK=32)."));
        Serial.println(F("Load cell: weight will read 0 until the HX711 replies."));
        return;
    }

    Serial.printf("Load cell: SPS = %.1f, conversion = %.2f ms\n",
                  loadCell.getSPS(), loadCell.getConversionTime());
    if (loadCell.getSPS() < 7.0f) {
        Serial.println(F("  WARNING: sampling rate too low - check wiring."));
    }
    Serial.println(F("HX711 load cell ready (kg). Waiting for TARE command."));

    loadCellLastPrintMs = millis();
}

// Call from loop() as often as possible — one HX711_ADC conversion per call.
bool updateLoadCell() {
    if (loadCell.update()) {
        loadCellKg         = loadCell.getData();
        loadCellLastDataMs = millis();
        return true;
    }
    return false;
}

// True while fresh samples keep arriving
bool loadCellReady() {
    return (loadCellLastDataMs != 0) &&
           ((millis() - loadCellLastDataMs) < LOADCELL_READY_TIMEOUT_MS);
}

// Returns weight in KG. 0 when the sensor is dead; negative noise clamped.
// (getData() is a cheap moving-average lookup — it does not block.)
float readLoadCell() {
    if (!loadCellReady()) {
        return 0.0f;
    }
    float kg = loadCell.getData();
    if (!isfinite(kg)) return 0.0f;
    if (kg < 0.0f) kg = 0.0f;
    return kg;
}

// Zero the scale (call with nothing on it)
void tareLoadCell() {
    if (!loadCellReady()) {
        Serial.println(F("HX711 not ready - tare aborted."));
        return;
    }

    Serial.print(F("Taring... "));
    loadCell.tare();                    // blocking, ~1-3 s at 10 SPS

    if (loadCell.getTareTimeoutFlag()) {
        Serial.println(F("TIMEOUT!"));
        Serial.println(F("  Check that the HX711 is connected and powered."));
        return;
    }

    loadCellKg         = loadCell.getData();
    loadCellLastDataMs = millis();
    Serial.println(F("OK. Place known weight, then send CALIBRATE:<kg>."));
}

// Fill the moving-average dataset with fresh conversions, but never hang if
// the HX711 stops responding (HX711_ADC's own refreshDataSet() is unbounded).
static bool loadCellRefreshDataSet(uint32_t timeoutMs) {
    int      remaining = loadCell.getSamplesInUse() + 2;   // + ignore-high/low slots
    uint32_t start     = millis();

    while (remaining > 0) {
        if (loadCell.update()) {
            loadCellKg         = loadCell.getData();
            loadCellLastDataMs = millis();
            remaining--;
        }
        if ((millis() - start) > timeoutMs) return false;
        delay(1);
    }
    return true;
}

// Run calibration against a known weight in KG.
// Step 1: send TARE         (empty scale)
// Step 2: place known weight
// Step 3: send CALIBRATE:<kg>
// The computed factor is saved to NVS and applied immediately.
void calibrateLoadCell(float known_kg) {
    if (!(known_kg > 0.0f)) {
        Serial.println(F("Calibration weight must be greater than 0."));
        return;
    }
    if (!loadCellReady()) {
        Serial.println(F("HX711 not ready - calibration aborted."));
        return;
    }

    Serial.printf("Measuring %.3f kg reference...\n", known_kg);

    // Keep the previous factor so a failed calibration can be rolled back
    float oldFactor = loadCell.getCalFactor();

    if (!loadCellRefreshDataSet(3000)) {             // bounded refill of the filter
        Serial.println(F("Calibration FAILED: HX711 stopped responding."));
        return;
    }
    float factor = loadCell.getNewCalibration(known_kg);   // also applies it

    if (!loadCellFactorValid(factor)) {
        loadCell.setCalFactor(oldFactor);
        Serial.println(F("Calibration FAILED: no load detected / invalid reading."));
        Serial.println(F("  Check that the weight is on the platform and the HX711 is wired."));
        return;
    }

    loadCellKg         = loadCell.getData();
    loadCellLastDataMs = millis();
    loadCellSaveFactor(factor);                      // persist across reboots

    Serial.println(F("--- Calibration complete ---"));
    Serial.printf("Calibration factor: %.6f\n", factor);
    Serial.printf("Reading now       : %.3f kg\n", loadCellKg);
    Serial.println(F("Factor saved to NVS. Will be restored on next power-up."));
}

// ------------------------------------------------------------
// Bench serial interface
// ------------------------------------------------------------
void loadCellPrintWeight() {
    if (!loadCellReady()) {
        Serial.println(F("HX711 not ready - no reading."));
        return;
    }
    float kg = readLoadCell();
    Serial.printf("W: %.3f kg  (%.0f g)\n", kg, kg * 1000.0f);
}

void loadCellPrintStatus() {
    Serial.println(F("--- LOAD CELL STATUS ---"));
    Serial.printf("HX711 ready       : %s\n", loadCellReady() ? "yes" : "no");
    Serial.printf("Calibration factor: %.6f\n", getCalibrationFactor());
    Serial.printf("Sampling rate     : %.1f SPS\n", loadCell.getSPS());
    Serial.printf("Stream            : %s\n", loadCellStreamEnabled ? "ON" : "OFF");
    Serial.printf("Interval          : %lu ms\n", (unsigned long)loadCellPrintIntervalMs);
    if (loadCellReady()) {
        Serial.printf("Weight            : %.3f kg\n", readLoadCell());
    }
    Serial.println(F("------------------------"));
}

void loadCellPrintHelp() {
    Serial.println();
    Serial.println(F("=== 200 kg Load Cell (HX711_ADC) - command list ==="));
    Serial.println(F("  HELP                 this list"));
    Serial.println(F("  TARE                 zero with an empty platform"));
    Serial.println(F("  CALIBRATE:<kg>       calibrate with a known weight"));
    Serial.println(F("  READ                 print one reading"));
    Serial.println(F("  STATUS               show factor / state / weight"));
    Serial.println(F("  FACTOR               show the active factor"));
    Serial.println(F("  FACTOR:<value>       set factor manually and save"));
    Serial.println(F("  RESET_CAL            clear stored calibration"));
    Serial.println(F("  STREAM:ON | OFF      periodic weight output"));
    Serial.println(F("  INTERVAL:<ms>        periodic output period"));
    Serial.println(F("==================================================="));
    Serial.println();
}

void loadCellHandleCommand(String line) {
    line.trim();
    if (line.length() == 0) return;

    Serial.printf("[CMD] %s\n", line.c_str());

    if (line.equalsIgnoreCase("HELP")) {
        loadCellPrintHelp();

    } else if (line.equalsIgnoreCase("TARE")) {
        tareLoadCell();

    } else if (line.startsWith(F("CALIBRATE:"))) {
        float kg = line.substring(10).toFloat();
        if (!(kg > 0.0f)) {
            Serial.println(F("Invalid weight. Usage: CALIBRATE:<kg>  (e.g. CALIBRATE:20.5)"));
            return;
        }
        calibrateLoadCell(kg);

    } else if (line.equalsIgnoreCase("READ") || line.equalsIgnoreCase("LOADCELL:READ")) {
        loadCellPrintWeight();

    } else if (line.equalsIgnoreCase("STATUS") || line.equalsIgnoreCase("STATUS?")) {
        loadCellPrintStatus();

    } else if (line.equalsIgnoreCase("FACTOR")) {
        Serial.printf("Calibration factor: %.6f\n", getCalibrationFactor());

    } else if (line.startsWith(F("FACTOR:"))) {
        float f = line.substring(7).toFloat();
        if (!loadCellFactorValid(f)) {
            Serial.println(F("Invalid factor. Usage: FACTOR:<value>  (e.g. FACTOR:420.5)"));
            return;
        }
        setCalibrationFactor(f);
        Serial.printf("Factor set and saved: %.6f\n", f);

    } else if (line.equalsIgnoreCase("RESET_CAL")) {
        clearCalibration();
        Serial.println(F("Stored calibration cleared. Default factor active."));

    } else if (line.startsWith(F("STREAM:"))) {
        String arg = line.substring(7);
        arg.trim();
        if (arg.equalsIgnoreCase("ON") || arg == "1") {
            loadCellStreamEnabled = true;
            Serial.println(F("Stream ON."));
        } else if (arg.equalsIgnoreCase("OFF") || arg == "0") {
            loadCellStreamEnabled = false;
            Serial.println(F("Stream OFF."));
        } else {
            Serial.println(F("Usage: STREAM:ON  or  STREAM:OFF"));
        }

    } else if (line.startsWith(F("INTERVAL:"))) {
        long ms = line.substring(9).toInt();
        if (ms < 100) {
            Serial.println(F("Interval too small. Minimum 100 ms."));
            return;
        }
        loadCellPrintIntervalMs = (uint32_t)ms;
        Serial.printf("Interval set to %lu ms.\n", (unsigned long)loadCellPrintIntervalMs);

    } else {
        Serial.printf("Unknown command: %s\n", line.c_str());
        Serial.println(F("Send HELP for the command list."));
    }
}

void loadCellReadSerial() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n') {
            loadCellHandleCommand(loadCellCmdLine);
            loadCellCmdLine = "";
        } else if (c != '\r') {
            if (loadCellCmdLine.length() < 64) loadCellCmdLine += c;   // runaway guard
        }
    }
}

void loadCellUpdateStream() {
    if (!loadCellStreamEnabled) return;
    uint32_t now = millis();
    if (now - loadCellLastPrintMs >= loadCellPrintIntervalMs) {
        loadCellLastPrintMs = now;
        loadCellPrintWeight();
    }
}

// Call every loop(): parses bench commands + prints the optional weight stream.
void loadCellSerialTask() {
    loadCellReadSerial();
    loadCellUpdateStream();
}

#endif // LOADCELL_CONFIG_H
