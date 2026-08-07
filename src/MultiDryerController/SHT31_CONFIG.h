// SHT31_CONFIG.h
// Multi Dryer Controller — non-blocking SHT31 temperature/humidity reader.
//
// Adapted from references/loadcell/FishDryer/SHT31_CONFIG.h:
//   - FishDryer used SoftwareWire on the Nano because its hardware I2C was
//     occupied by the HMI slave link. The ESP32 controller talks to the HMI
//     wirelessly (ESP-NOW), so the hardware I2C bus is free — we use Wire on
//     SHT31_SDA_PIN/SHT31_SCL_PIN (GPIO 21/22).
//   - Same non-blocking state machine, CRC-8 check, and value clamping.
//
// Call initSHT31()  once in setup().
// Call updateSHT31() every loop() iteration (non-blocking state machine).
// CURRENT_TEMPERATURE and CURRENT_HUMIDITY are updated automatically.

#ifndef SHT31_CONFIG_H
#define SHT31_CONFIG_H

#include <Wire.h>
#include "PINS_CONFIG.h"

// ── SHT31 constants ───────────────────────────────────────────────────────────
#define SHT31_ADDR              0x44    // change to 0x45 if ADDR pin is tied HIGH
#define SHT31_READ_INTERVAL_MS  2000    // sensor poll rate
#define SHT31_MEAS_DELAY_MS     20      // high-repeatability measurement time

// Single-shot command: high repeatability, clock-stretching disabled
#define SHT31_CMD_MSB           0x24
#define SHT31_CMD_LSB           0x00

// Externals defined in MultiDryerController.ino
extern bool   sht31OK;
extern float  CURRENT_HUMIDITY;
extern double CURRENT_TEMPERATURE;

// ── State machine ─────────────────────────────────────────────────────────────
enum SHT31Phase : uint8_t { SHT_IDLE, SHT_WAITING };

static SHT31Phase    _sht31Phase      = SHT_IDLE;
static unsigned long _sht31LastRead   = 0;
static unsigned long _sht31TriggerMs  = 0;
static uint8_t       _sht31Fails      = 0;

// ── CRC-8 (polynomial 0x31, init 0xFF) ───────────────────────────────────────
static uint8_t _sht31CRC(const uint8_t* data, uint8_t len) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
  }
  return crc;
}

// ── Init ──────────────────────────────────────────────────────────────────────
void initSHT31() {
  Wire.begin(SHT31_SDA_PIN, SHT31_SCL_PIN, 100000);  // hardware I2C, GPIO 21/22

  // Boot-time presence check (first poll would otherwise take ~2 s to fail)
  Wire.beginTransmission(SHT31_ADDR);
  sht31OK = (Wire.endTransmission() == 0);

  Serial.print(F("[SHT31] I2C on GPIO "));
  Serial.print(SHT31_SDA_PIN);
  Serial.print(F("/"));
  Serial.print(SHT31_SCL_PIN);
  Serial.print(sht31OK ? F(" — sensor found (0x44)") : F(" — NO SENSOR!"));
  Serial.println();
}

// ── Non-blocking state machine — call every loop() ───────────────────────────
void updateSHT31() {
  unsigned long now = millis();

  switch (_sht31Phase) {

    case SHT_IDLE:
      if (now - _sht31LastRead < SHT31_READ_INTERVAL_MS) return;
      Wire.beginTransmission(SHT31_ADDR);
      Wire.write(SHT31_CMD_MSB);
      Wire.write(SHT31_CMD_LSB);
      if (Wire.endTransmission() == 0) {
        _sht31Phase     = SHT_WAITING;
        _sht31TriggerMs = now;
      } else {
        _sht31Fails++;
        _sht31LastRead = now;
        // NOTE: noInterrupts() only masks interrupts on the current core — it is
        // NOT a cross-task critical section on ESP32/FreeRTOS. Fine while all
        // access happens in loop(); guard with a mutex if ESP-NOW/HMI code ever
        // touches these globals from another task.
        noInterrupts(); sht31OK = false; interrupts();
      }
      break;

    case SHT_WAITING:
      if (now - _sht31TriggerMs < SHT31_MEAS_DELAY_MS) return;
      _sht31LastRead = now;
      _sht31Phase    = SHT_IDLE;

      if (Wire.requestFrom(SHT31_ADDR, 6) != 6) {
        _sht31Fails++;
        noInterrupts(); sht31OK = false; interrupts();
        return;
      }
      {
        uint8_t buf[6];
        for (uint8_t i = 0; i < 6; i++) buf[i] = Wire.read();

        if (_sht31CRC(buf, 2) != buf[2] || _sht31CRC(buf + 3, 2) != buf[5]) {
          _sht31Fails++;
          noInterrupts(); sht31OK = false; interrupts();
          return;
        }

        uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
        uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];
        float temp = -45.0f + 175.0f * ((float)rawT / 65535.0f);
        float hum  = 100.0f * ((float)rawH / 65535.0f);

        if (temp >= -40.0f && temp <= 125.0f && hum >= 0.0f && hum <= 100.0f) {
          noInterrupts();
          CURRENT_TEMPERATURE = (double)temp;
          CURRENT_HUMIDITY    = hum;
          sht31OK             = true;
          interrupts();
          _sht31Fails = 0;
        } else {
          _sht31Fails++;
          noInterrupts(); sht31OK = false; interrupts();
        }
      }
      break;
  }
}

// ── Read helpers (for PID / HMI status) ──────────────────────────────────────
float getTemperature() { return (float)CURRENT_TEMPERATURE; }
float getHumidity()    { return CURRENT_HUMIDITY; }
uint8_t getSHT31Fails() { return _sht31Fails; }   // diagnostic: 0 = healthy, >0 = sensor errors since last good read

#endif // SHT31_CONFIG_H
