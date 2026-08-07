// MultiDryerController.ino
// ESP32 38-pin controller.
// Hardware wiring: references/plans/hardware-wiring.md
// Pin map:         PINS_CONFIG.h

#include "PINS_CONFIG.h"
#include "SHT31_CONFIG.h"

// ── Shared SHT31 state (consumed by SHT31_CONFIG.h) ──────────────────────────
bool   sht31OK             = false;
float  CURRENT_HUMIDITY    = 0.0f;
double CURRENT_TEMPERATURE = 0.0;

unsigned long _lastSht31Print = 0;

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

  // SHT31 temperature/humidity sensor (hardware I2C on GPIO 21/22)
  initSHT31();
}

void loop() {
  // Non-blocking SHT31 read state machine — call every iteration
  updateSHT31();

  // Debug print every 5 s (replace with ESP-NOW status packets once the
  // controller <-> HMI link is implemented)
  unsigned long now = millis();
  if (now - _lastSht31Print >= 5000) {
    _lastSht31Print = now;
    Serial.printf("[SHT31] temp=%.2f C  hum=%.1f %%  status=%s\n",
                  (float)CURRENT_TEMPERATURE,
                  CURRENT_HUMIDITY,
                  sht31OK ? "OK" : "NO SENSOR");
  }
}
