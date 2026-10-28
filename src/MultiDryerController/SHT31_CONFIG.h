// SHT31_CONFIG.h
// Multi Dryer Controller — SHT30/SHT31 temperature/humidity reader.
//
// Uses the Adafruit_SHT31 library — the same code path proven on the FishDryer
// hardware — but keeps the multi-dryer's polling cadence: one measurement every
// SHT31_READ_INTERVAL_MS, never inside the fast control loop. A single
// readBoth() blocks ~15-30 ms once per 2 s, which is harmless here (the HX711
// is polled from loop() and at worst misses one 10 SPS conversion window).
//
// Address: modules answer at 0x44 (default) or 0x45 (ADDR pin HIGH). Both are
// probed automatically instead of hard-coding one, and the sensor is re-detected
// if it stops answering (brown-out / loose wire) without rebooting.
//
// Call initSHT31()  once in setup().
// Call updateSHT31() every loop() iteration (rate-limited internally).
// CURRENT_TEMPERATURE and CURRENT_HUMIDITY are updated automatically.

#ifndef SHT31_CONFIG_H
#define SHT31_CONFIG_H

#include <Wire.h>
#include <Adafruit_SHT31.h>
#include "PINS_CONFIG.h"

// ── SHT31 constants ───────────────────────────────────────────────────────────
#define SHT31_ADDR_PRIMARY        0x44   // ADDR pin LOW (module default)
#define SHT31_ADDR_ALTERNATE      0x45   // ADDR pin HIGH
#define SHT31_READ_INTERVAL_MS    2000   // sensor poll rate
#define SHT31_REINIT_AFTER_FAILS  3      // consecutive failures before re-probing

// Externals defined in MultiDryerController.ino
extern bool   sht31OK;
extern float  CURRENT_HUMIDITY;
extern double CURRENT_TEMPERATURE;

// ── State ─────────────────────────────────────────────────────────────────────
static Adafruit_SHT31 sht31;
static unsigned long  _sht31LastRead = 0;
static uint8_t        _sht31Addr     = 0;    // 0 = not detected
static uint8_t        _sht31Fails    = 0;

// ── Helpers ──────────────────────────────────────────────────────────────────
static bool _sht31Probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// Scan the bus once and log every device that answers — makes wiring and
// address problems obvious instead of a bare "not found".
static void _sht31ScanBus() {
  Serial.print(F("[SHT31] I2C scan:"));
  uint8_t found = 0;
  for (uint8_t a = 0x08; a < 0x78; a++) {
    if (_sht31Probe(a)) {
      Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  if (!found) Serial.print(F(" no devices"));
  Serial.println();
}

// Bring the sensor up at one address (Adafruit begin() = soft reset + status read).
static bool _sht31BeginAt(uint8_t addr) {
  if (!sht31.begin(addr)) return false;
  _sht31Addr  = addr;
  sht31OK     = true;
  _sht31Fails = 0;
  Serial.printf("[SHT31] Sensor ready at 0x%02X\n", addr);
  return true;
}

static bool _sht31Detect() {
  return _sht31BeginAt(SHT31_ADDR_PRIMARY) || _sht31BeginAt(SHT31_ADDR_ALTERNATE);
}

// ── Init ──────────────────────────────────────────────────────────────────────
void initSHT31() {
  Wire.begin(SHT31_SDA_PIN, SHT31_SCL_PIN, 100000);  // hardware I2C, GPIO 21/22

  Serial.print(F("[SHT31] I2C on GPIO "));
  Serial.print(SHT31_SDA_PIN);
  Serial.print(F("/"));
  Serial.print(SHT31_SCL_PIN);
  Serial.println(F(" @100kHz"));

  _sht31LastRead = millis();
  if (!_sht31Detect()) {
    sht31OK = false;
    Serial.println(F("[SHT31] NO SENSOR — check SDA/SCL, 3.3 V and ADDR strap"));
    _sht31ScanBus();
  }
}

// ── Poll (rate-limited — call every loop()) ──────────────────────────────────
void updateSHT31() {
  unsigned long now = millis();
  if (now - _sht31LastRead < SHT31_READ_INTERVAL_MS) return;
  _sht31LastRead = now;

  // Not detected (or dropped off the bus): try to bring it back.
  if (_sht31Addr == 0) {
    if (!_sht31Detect()) return;
    // fall through and take the first reading in this same call
  }

  float t = NAN, h = NAN;
  if (!sht31.readBoth(&t, &h) || isnan(t) || isnan(h)) {
    if (_sht31Fails < 255) _sht31Fails++;
    sht31OK = false;
    if (_sht31Fails >= SHT31_REINIT_AFTER_FAILS) {
      _sht31Addr  = 0;    // force a fresh begin() on the next poll
      _sht31Fails = 0;
    }
    return;
  }

  // Datasheet operating range; reject obviously bogus frames.
  if (t < -40.0f || t > 125.0f || h < 0.0f || h > 100.0f) {
    if (_sht31Fails < 255) _sht31Fails++;
    sht31OK = false;
    return;
  }

  noInterrupts();
  CURRENT_TEMPERATURE = (double)t;
  CURRENT_HUMIDITY    = h;
  sht31OK             = true;
  interrupts();
  _sht31Fails = 0;
}

// ── Read helpers (for PID / HMI status) ──────────────────────────────────────
float getTemperature() { return (float)CURRENT_TEMPERATURE; }
float getHumidity()    { return CURRENT_HUMIDITY; }
uint8_t getSHT31Fails() { return _sht31Fails; }   // diagnostic: 0 = healthy, >0 = errors since last good read
uint8_t getSHT31Address() { return _sht31Addr; }  // 0 = not detected, else 0x44 / 0x45

#endif // SHT31_CONFIG_H
