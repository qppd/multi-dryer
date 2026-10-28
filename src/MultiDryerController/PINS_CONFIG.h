// PINS_CONFIG.h
// Multi Dryer Controller — centralized pin assignments (ESP32 38-pin DevKitC)
//
// Wiring plan: docs/schematic/hardware-wiring.md
//
// Boot/WiFi safety rules applied here:
//   - No strapping pins (0, 2, 4, 5, 12, 15) — flashing/boot always works.
//   - No flash pins (6-11), no UART0 (1/3), no GPIO 14 (PWM burst at boot).
//   - All outputs are forced LOW at the top of setup() → fail-safe OFF at boot.
//   - No analog reads on ADC2 pins (unusable while WiFi/ESP-NOW is active);
//     analog-capable pins are used as digital outputs only.

#ifndef PINS_CONFIG_H
#define PINS_CONFIG_H

// =========================
// SSR / Power Outputs
// =========================
// SSR1: PTC Heater (power stage)   → GPIO 25
// SSR3: Inlet Air Fan              → GPIO 26
// SSR4: Exhaust Fan                → GPIO 27
// (3-SSR build: SSR1 = heater, SSR3 = inlet fan, SSR4 = exhaust fan. The old
//  SSR2 exhaust-outlet channel was removed; venting is done by the exhaust fan
//  alone. GPIO 13 is now spare.)
#ifndef SSR1_PIN
#define SSR1_PIN 25
#endif

#ifndef SSR3_PIN
#define SSR3_PIN 26
#endif

#ifndef SSR4_PIN
#define SSR4_PIN 27
#endif

// =========================
// HX711 Load Cell
// =========================
// DT/DOUT is an output from the HX711 and an input to the ESP32.
// SCK must be a clean digital output.
// (GPIO 35 also works and is input-only; GPIO 33 is what the proven
//  200 kg / HX711_ADC bench setup uses.)
#ifndef LOADCELL_DOUT_PIN
#define LOADCELL_DOUT_PIN 33
#endif

#ifndef LOADCELL_SCK_PIN
#define LOADCELL_SCK_PIN 32
#endif

// =========================
// Sensors — I2C bus
// =========================
// SHT31 temperature/humidity sensor (I2C) — temperature feedback for PID control.
// NOTE: this is the ONLY I2C device. The controller ⇄ HMI link is ESP-NOW only
// (wireless) — no wired comms (no UART, no HMI I2C) are used.
#ifndef SHT31_SDA_PIN
#define SHT31_SDA_PIN 21
#endif

#ifndef SHT31_SCL_PIN
#define SHT31_SCL_PIN 22
#endif

// Spare digital: GPIO 13, 16, 17, 18, 19, 23 (16/17 = WROOM-32 only — do NOT
// use on WROVER modules, where they are PSRAM).
// Spare input-only (future analog, ADC1/WiFi-safe): GPIO 34, 35, 36, 39.

#endif // PINS_CONFIG_H
