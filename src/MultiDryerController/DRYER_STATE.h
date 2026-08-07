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

#include "espnow_protocol.h"
#include "LOADCELL_CONFIG.h"
#include "PID_CONFIG.h"

#define DRYER_TICK_MS  1000

// ── Tracking state (single writer: updateDrying(), loop context) ─────────────
static uint8_t       _state           = DSTATE_IDLE;
static float         _waterLossPct    = 0.0f;
static float         _waterLossTarget = 0.0f;   // 0 = auto-complete disabled
static float         _weightKg        = 0.0f;   // cached, refreshed at 1 Hz
static float         _initialWeightKg = 0.0f;
static unsigned long _runMs           = 0;      // drying time, pauses excluded
static unsigned long _lastTickMs      = 0;
static uint32_t      _runtimeSeconds  = 0;
static uint32_t      _estimatedEDT    = 0;      // seconds; 0 = unknown

// ── Getters (used by the ESP-NOW status packet) ──────────────────────────────
uint8_t  getDryerState()      { return _state; }
float    getWaterLoss()       { return _waterLossPct; }
float    getWaterLossTarget() { return _waterLossTarget; }
float    getWeightKg()        { return _weightKg; }
uint32_t getRuntimeSeconds()  { return _runtimeSeconds; }
uint32_t getEstimatedEDT()    { return _estimatedEDT; }

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
    _state = DSTATE_DRYING;
    if (TEMPERATURE_SETPOINT <= 0.0) {
        Serial.println(F("[DRYER] WARNING: no setpoint set — send SET_TEMPERATURE"));
    }
    startPID();
    Serial.printf("[DRYER] started. Initial weight %.3f kg\n", _initialWeightKg);
}

void stopDrying() {
    stopPID();   // all SSRs off
    _state = DSTATE_IDLE;
    _runMs = 0; _runtimeSeconds = 0; _estimatedEDT = 0;
    _waterLossPct = 0; _initialWeightKg = 0;
    Serial.println(F("[DRYER] stopped"));
}

void pauseDrying() {
    if (_state != DSTATE_DRYING) return;
    stopPID();   // all SSRs off
    _state = DSTATE_PAUSED;
    Serial.println(F("[DRYER] paused"));
}

void resumeDrying() {
    if (_state != DSTATE_PAUSED) return;
    _state = DSTATE_DRYING;
    startPID();
    Serial.println(F("[DRYER] resumed"));
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
    }
}

#endif // DRYER_STATE_H
