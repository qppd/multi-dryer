// MultiDryerController.ino
// ESP32 38-pin controller.
// Hardware wiring: references/plans/hardware-wiring.md
// Pin map:         PINS_CONFIG.h
// Protocol:        espnow_protocol.h (shared with the HMI, keep byte-identical)

#include "PINS_CONFIG.h"
#include "SHT31_CONFIG.h"
#include "LOADCELL_CONFIG.h"
#include "PID_CONFIG.h"
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

// ── SSR driver (declared extern in PID_CONFIG.h) ─────────────────────────────
// SSR1 = PTC heater, SSR2 = exhaust outlet, SSR3 = inlet fan, SSR4 = exhaust fan
void operateSSR(int relayIndex, bool state) {
    int pin;
    switch (relayIndex) {
        case 1: pin = SSR1_PIN; break;
        case 2: pin = SSR2_PIN; break;
        case 3: pin = SSR3_PIN; break;
        case 4: pin = SSR4_PIN; break;
        default: return;
    }
    digitalWrite(pin, state ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Fail-safe: force all SSR outputs OFF before anything else.
  // (10kΩ pull-downs on the PCB already guarantee OFF during the boot window;
  //  this keeps them OFF once firmware runs.)
  pinMode(SSR1_PIN, OUTPUT); digitalWrite(SSR1_PIN, LOW);
  pinMode(SSR2_PIN, OUTPUT); digitalWrite(SSR2_PIN, LOW);
  pinMode(SSR3_PIN, OUTPUT); digitalWrite(SSR3_PIN, LOW);
  pinMode(SSR4_PIN, OUTPUT); digitalWrite(SSR4_PIN, LOW);

  initLoadCell();   // HX711 weight sensor (DOUT=35, SCK=32)
  initSHT31();      // temperature/humidity (I2C 21/22)
  initPID();        // PTC heater temperature control (starts in MANUAL = off)
  initEspNow();     // wireless link to the HMI board
}

void loop() {
  // Non-blocking SHT31 read state machine — call every iteration
  updateSHT31();

  // PID: compute + drive SSRs (PID_v1 throttles itself to its 1 s sample time)
  pidCOMPUTE();

  // ESP-NOW: process HMI commands + send periodic status packets (1 Hz)
  espnowUpdate();

  // Debug print every 5 s — mirrors what the HMI receives over ESP-NOW
  unsigned long now = millis();
  if (now - _lastStatusPrint >= 5000) {
    _lastStatusPrint = now;
    Serial.printf("[STATUS] temp=%.2f C  hum=%.1f %%  weight=%.3f kg  "
                  "setpoint=%.1f C  pid=%.0f  sht31=%s  hmi=%s\n",
                  (float)CURRENT_TEMPERATURE,
                  CURRENT_HUMIDITY,
                  readLoadCell(),
                  TEMPERATURE_SETPOINT,
                  PID_OUTPUT,
                  sht31OK ? "OK" : "NO SENSOR",
                  hmiReachable ? "reachable" : "not seen");
  }
}
