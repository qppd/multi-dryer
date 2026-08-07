// DRYER_STATE.h
// Multi Dryer Controller — drying state machine + water-loss tracking.
//
// States (DSTATE_* from espnow_protocol.h — shared with the HMI):
//       IDLE ──START──► DRYING ──auto (target reached)──► COMPLETE
//        ▲              │  ▲
//        │ STOP         ▼  │ RESUME
//                     PAUSED
//
// Water loss % = (initialWeight − currentWeight) / initialWeight × 100.
// When waterLossTarget > 0 and waterLoss reaches it, the cycle auto-completes
// (heater/fans off). Target 0.0 = auto-complete disabled (STOP_DRYING ends it).
//
// All weight reads flow through this module (1 Hz cache) so the ESP-NOW status
// packet and the water-loss math always use the same value.

#ifndef DRYER_STATE_H
#define DRYER_STATE_H

#include <Preferences.h>
#include "espnow_protocol.h"
#include "LOADCELL_CONFIG.h"
#include "PID_CONFIG.h"

#define DRYER_TICK_MS  1000

// ── Session persistence (NVS) ────────────────────────────────────────────────
// Lets a drying session survive a controller reboot / watchdog reset:
//   - initial weight AND the HX711 raw offset (the zero reference) are saved,
//     so weight/water-loss keep meaning even after a full power cycle, where
//     the HX711 offset drifts. On restore the offset is re-applied (no re-tare).
//   - elapsed runtime, water-loss target and temperature setpoint are restored.
//   - An active session (DRYING/PAUSED) resumes automatically on boot;
//     DRYING re-enables the PID. TARE/CALIBRATE are blocked while a session is
//     active (see espnow_link.h) so the HMI's boot-time TARE cannot destroy the
//     restored zero reference.
#define DRYER_PREFS_NS          "drying"
#define DRYER_PREFS_MAGIC       0xD1
#define DRYER_SAVE_INTERVAL_MS  30000   // periodic save cadence while drying

// ── Tracking state (single writer: updateDrying(), loop context) ─────────────
static uint8_t       _state           = DSTATE_IDLE;
static float         _waterLossPct    = 0.0f;
static float         _waterLossTarget = 0.0f;   // 0 = auto-complete disabled
static float         _weightKg        = 0.0f;   // cached, refreshed at 1 Hz
static float         _initialWeightKg = 0.0f;
static unsigned long _runMs           = 0;      // drying time, pauses excluded
static unsigned long _lastTickMs      = 0;
static unsigned long _lastSaveMs      = 0;      // periodic session-save throttle
static uint32_t      _runtimeSeconds  = 0;
static uint32_t      _estimatedEDT    = 0;      // seconds; 0 = unknown

// ── Getters (used by the ESP-NOW status packet) ──────────────────────────────
uint8_t  getDryerState()      { return _state; }
float    getWaterLoss()       { return _waterLossPct; }
float    getWaterLossTarget() { return _waterLossTarget; }
float    getWeightKg()        { return _weightKg; }
uint32_t getRuntimeSeconds()  { return _runtimeSeconds; }
uint32_t getEstimatedEDT()    { return _estimatedEDT; }

// Forward declarations — persistence functions are defined at the bottom
static void saveDryingSession(uint8_t state);
static void clearDryingSession();
static void persistDryerConfig();
static void restoreDryingSession();

// ── Control ───────────────────────────────────────────────────────────────────
void setWaterLossTarget(float pct) {
    _waterLossTarget = constrain(pct, 0.0f, 100.0f);
    Serial.printf("[DRYER] water-loss target %.1f %%\n", _waterLossTarget);
}

void initDrying() {
    _state = DSTATE_IDLE;
    _waterLossPct = 0.0f; _waterLossTarget = 0.0f;
    _weightKg = 0.0f; _initialWeightKg = 0.0f;
    _runMs = 0; _runtimeSeconds = 0; _estimatedEDT = 0;
    _lastTickMs = millis();
    _lastSaveMs = millis();
    restoreDryingSession();   // auto-resume an interrupted session, if any
}

// Fresh session: capture initial weight, start PID, begin tracking
void startDrying() {
    if (_state == DSTATE_DRYING) {
        Serial.println(F("[DRYER] already drying"));
        return;
    }
    if (_state != DSTATE_IDLE) {
        Serial.println(F("[DRYER] starting fresh session (restart)"));
    }
    _initialWeightKg = readLoadCell();
    _weightKg        = _initialWeightKg;
    _runMs = 0; _runtimeSeconds = 0; _estimatedEDT = 0; _waterLossPct = 0;
    _lastTickMs = millis();
    _lastSaveMs = millis();
    _state = DSTATE_DRYING;
    if (TEMPERATURE_SETPOINT <= 0.0) {
        Serial.println(F("[DRYER] WARNING: no setpoint set — send SET_TEMPERATURE"));
    }
    startPID();
    saveDryingSession(DSTATE_DRYING);
    Serial.printf("[DRYER] started. Initial weight %.3f kg\n", _initialWeightKg);
}

void stopDrying() {
    stopPID();   // all SSRs off
    _state = DSTATE_IDLE;
    _runMs = 0; _runtimeSeconds = 0; _estimatedEDT = 0;
    _waterLossPct = 0; _initialWeightKg = 0;
    clearDryingSession();   // session over — nothing to resume later
    Serial.println(F("[DRYER] stopped"));
}

void pauseDrying() {
    if (_state != DSTATE_DRYING) return;
    stopPID();   // all SSRs off
    _state = DSTATE_PAUSED;
    saveDryingSession(DSTATE_PAUSED);
    Serial.println(F("[DRYER] paused (session saved)"));
}

void resumeDrying() {
    if (_state != DSTATE_PAUSED) return;
    _state = DSTATE_DRYING;
    startPID();
    saveDryingSession(DSTATE_DRYING);
    Serial.println(F("[DRYER] resumed (session saved)"));
}

// ── 1 Hz tick (call every loop(); throttled internally) ──────────────────────
void updateDrying() {
    unsigned long now = millis();
    if (now - _lastTickMs < DRYER_TICK_MS) return;
    unsigned long elapsed = now - _lastTickMs;
    _lastTickMs = now;

    // Weight cache — refreshed at 1 Hz in every state so the HMI always sees
    // live weight (single read source for status + water loss)
    _weightKg = readLoadCell();

    if (_state != DSTATE_DRYING) return;

    _runMs += elapsed;
    _runtimeSeconds = _runMs / 1000UL;

    // Water loss from the session's initial weight
    if (_initialWeightKg > 0.01f) {
        _waterLossPct = (_initialWeightKg - _weightKg) / _initialWeightKg * 100.0f;
        if (_waterLossPct < 0.0f) _waterLossPct = 0.0f;
        if (_waterLossPct > 100.0f) _waterLossPct = 100.0f;
    } else {
        _waterLossPct = 0.0f;
    }

    // Rate-based estimate: remaining % ÷ %/s
    if (_waterLossTarget > 0.0f && _waterLossPct < _waterLossTarget && _runtimeSeconds > 0) {
        float ratePctPerSec = _waterLossPct / (float)_runtimeSeconds;
        if (ratePctPerSec > 0.0001f) {
            _estimatedEDT = (uint32_t)((_waterLossTarget - _waterLossPct) / ratePctPerSec);
        } else {
            _estimatedEDT = 0;
        }
    }

    // Auto-complete when the target water loss is reached
    if (_waterLossTarget > 0.0f && _waterLossPct >= _waterLossTarget) {
        Serial.printf("[DRYER] COMPLETE — water loss %.1f %% reached target %.1f %%\n",
                      _waterLossPct, _waterLossTarget);
        _estimatedEDT = 0;   // no longer meaningful once complete
        stopPID();
        _state = DSTATE_COMPLETE;
        clearDryingSession();   // finished — nothing to resume later
    }

    // Periodic save while drying, so a mid-session reboot loses at most
    // DRYER_SAVE_INTERVAL_MS of elapsed runtime. Guarded on DRYING so the
    // auto-complete tick can never re-save a finished session as active.
    if (_state == DSTATE_DRYING && now - _lastSaveMs >= DRYER_SAVE_INTERVAL_MS) {
        _lastSaveMs = now;
        saveDryingSession(DSTATE_DRYING);
    }
}

// ── NVS session persistence ──────────────────────────────────────────────────
static void saveDryingSession(uint8_t state) {
    Preferences prefs;
    prefs.begin(DRYER_PREFS_NS, false);
    prefs.putUChar("magic", DRYER_PREFS_MAGIC);
    prefs.putUChar("state", state);
    prefs.putFloat("initW", _initialWeightKg);
    prefs.putULong("runMs", _runMs);
    prefs.putFloat("wlTgt", _waterLossTarget);
    prefs.putFloat("setpt", (float)TEMPERATURE_SETPOINT);
    prefs.putLong("hxoff", (int64_t)scale.get_offset());   // zero reference
    prefs.end();
}

static void clearDryingSession() {
    Preferences prefs;
    prefs.begin(DRYER_PREFS_NS, false);
    prefs.putUChar("state", DSTATE_IDLE);   // IDLE = no active session
    prefs.end();
}

// Persist the configured setpoint + water-loss target so a reboot keeps the
// HMI's values even when no session is active. Called from the ESP-NOW
// command handler whenever the HMI changes a parameter. Doesn't touch the
// session keys (initW/runMs/hxoff/state) — those are owned by
// saveDryingSession()/clearDryingSession(), which write the marker on every
// transition. Writing 'state' here could resurrect a completed session: the
// marker is IDLE after auto-complete, but COMPLETE written here would restore
// as DRYING with stale weight data.
static void persistDryerConfig() {
    Preferences prefs;
    prefs.begin(DRYER_PREFS_NS, false);
    prefs.putUChar("magic", DRYER_PREFS_MAGIC);
    prefs.putFloat("wlTgt", _waterLossTarget);
    prefs.putFloat("setpt", (float)TEMPERATURE_SETPOINT);
    prefs.end();
}

// Called from initDrying() — restores the last configured values and, if one
// was active, resumes the interrupted session.
static void restoreDryingSession() {
    Preferences prefs;
    prefs.begin(DRYER_PREFS_NS, true);
    if (prefs.getUChar("magic", 0) != DRYER_PREFS_MAGIC) { prefs.end(); return; }
    uint8_t savedState = prefs.getUChar("state", DSTATE_IDLE);

    // Config (setpoint + water-loss target) survives even when no session is
    // active, so a reboot keeps the HMI's configured values.
    TEMPERATURE_SETPOINT = (double)prefs.getFloat("setpt", (float)TEMPERATURE_SETPOINT);
    _waterLossTarget     = prefs.getFloat("wlTgt", _waterLossTarget);
    Serial.printf("[DRYER] config restored — setpoint %.1f C, wlTgt %.1f %%\n",
                  TEMPERATURE_SETPOINT, _waterLossTarget);

    if (savedState == DSTATE_IDLE) { prefs.end(); return; }

    _initialWeightKg     = prefs.getFloat("initW", 0.0f);
    _runMs               = prefs.getULong("runMs", 0);
    _runtimeSeconds      = _runMs / 1000UL;
    scale.set_offset((long)prefs.getLong("hxoff", 0));   // re-apply zero reference
    prefs.end();

    _state = (savedState == DSTATE_PAUSED) ? DSTATE_PAUSED : DSTATE_DRYING;
    _lastTickMs = millis();
    _lastSaveMs = millis();
    _weightKg   = readLoadCell();   // fresh read with the restored offset

    Serial.printf("[DRYER] RESUMED session (%s) — initW %.3f kg, elapsed %lus, "
                  "setpoint %.1f C, wlTgt %.1f %%\n",
                  _state == DSTATE_PAUSED ? "PAUSED" : "DRYING",
                  _initialWeightKg, (unsigned long)_runtimeSeconds,
                  TEMPERATURE_SETPOINT, _waterLossTarget);

    if (_state == DSTATE_DRYING) {
        startPID();   // continue heating — SHT31 vent-guard protects if sensor is lost
    }
}

#endif // DRYER_STATE_H
