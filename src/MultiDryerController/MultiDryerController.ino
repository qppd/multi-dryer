// MultiDryerController.ino
// ESP32 38-pin controller.
// Hardware wiring: references/plans/hardware-wiring.md
// Pin map:         PINS_CONFIG.h
// Protocol:        espnow_protocol.h (shared with the HMI, keep byte-identical)

#include "PINS_CONFIG.h"
#include "SHT31_CONFIG.h"
#include "LOADCELL_CONFIG.h"
#include "PID_CONFIG.h"
#include "DRYER_STATE.h"
#include "espnow_protocol.h"
#include "espnow_link.h"

// ── Shared SHT31 state (consumed by SHT31_CONFIG.h) ──────────────────────────
bool   sht31OK             = false;
float  CURRENT_HUMIDITY    = 0.0f;
double CURRENT_TEMPERATURE = 0.0;

// ── PID state (consumed by PID_CONFIG.h) ─────────────────────────────────────
// Default 0.0 = heater stays off until the HMI sends a target temperature.
double TEMPERATURE_SETPOINT = 0.0;
double PID_OUTPUT           = 0.0;

unsigned long _lastStatusPrint = 0;

const char* stateName(uint8_t s) {
  switch (s) {
    case DSTATE_IDLE:     return "IDLE";
    case DSTATE_DRYING:   return "DRYING";
    case DSTATE_PAUSED:   return "PAUSED";
    case DSTATE_COMPLETE: return "COMPLETE";
    default:              return "?";
  }
}

// ── SSR driver (declared extern in PID_CONFIG.h) ─────────────────────────────
// SSR1 = PTC heater, SSR3 = inlet fan, SSR4 = exhaust fan (3-SSR build)
//
// ssrStateFlags mirrors the REAL output state (used by the ESP-NOW status
// packet so the HMI always shows what is actually energized — PID-driven or
// manual override). Bit layout matches FLAG_HEATER / FLAG_FAN / FLAG_EXHAUST
// from espnow_protocol.h.
uint8_t ssrStateFlags = 0;

void operateSSR(int relayIndex, bool state) {
    int pin;
    uint8_t flag;
    switch (relayIndex) {
        case 1: pin = SSR1_PIN; flag = FLAG_HEATER; break;
        case 3: pin = SSR3_PIN; flag = FLAG_FAN; break;
        case 4: pin = SSR4_PIN; flag = FLAG_EXHAUST; break;
        default: return;
    }
    digitalWrite(pin, state ? HIGH : LOW);
    if (state) ssrStateFlags |= flag;
    else       ssrStateFlags &= (uint8_t)~flag;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Fail-safe: force all SSR outputs OFF before anything else — SSR inputs
  // float (high-Z) during the ~1 s boot window, so pins are driven LOW as
  // the first step of setup(), before WiFi/ESP-NOW init.
  pinMode(SSR1_PIN, OUTPUT); digitalWrite(SSR1_PIN, LOW);
  pinMode(SSR3_PIN, OUTPUT); digitalWrite(SSR3_PIN, LOW);
  pinMode(SSR4_PIN, OUTPUT); digitalWrite(SSR4_PIN, LOW);

  initLoadCell();   // HX711 weight sensor (DOUT=35, SCK=32)
  initSHT31();      // temperature/humidity (I2C 21/22)
  initPID();        // PTC heater temperature control (starts in MANUAL = off)
  initDrying();     // drying state machine (IDLE)
  initEspNow();     // wireless link to the HMI board
}

void loop() {
  // Non-blocking SHT31 read state machine — call every iteration
  updateSHT31();

  // PID: compute + drive SSRs (PID_v1 throttles itself to its 1 s sample time)
  pidCOMPUTE();

  // Drying state machine: 1 Hz tick (weight cache, water loss, auto-complete)
  updateDrying();

  // ESP-NOW: process HMI commands + send periodic status packets (1 Hz)
  espnowUpdate();

  // Debug print every 5 s — mirrors what the HMI receives over ESP-NOW
  unsigned long now = millis();
  if (now - _lastStatusPrint >= 5000) {
    _lastStatusPrint = now;
    Serial.printf("[STATUS] %-8s temp=%.2f C  hum=%.1f %%  weight=%.3f kg  "
                  "loss=%.1f%%  setpoint=%.1f C  pid=%.0f  hmi=%s\n",
                  stateName(getDryerState()),
                  (float)CURRENT_TEMPERATURE,
                  CURRENT_HUMIDITY,
                  getWeightKg(),
                  getWaterLoss(),
                  TEMPERATURE_SETPOINT,
                  PID_OUTPUT,
                  hmiReachable ? "reachable" : "not seen");
  }
}
