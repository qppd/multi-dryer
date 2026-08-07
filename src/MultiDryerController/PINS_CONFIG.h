// PINS_CONFIG.h
// Multi Dryer Controller — centralized pin assignments (ESP32 38-pin DevKitC)
//
// Wiring plan: references/plans/hardware-wiring.md
//
// Boot/WiFi safety rules applied here:
//   - No strapping pins (0, 2, 4, 5, 12, 15) — flashing/boot always works.
//   - No flash pins (6-11), no UART0 (1/3), no GPIO 14 (PWM burst at boot).
//   - All outputs have 10kΩ pull-downs on the PCB → fail-safe OFF at boot.
//   - No analog reads on ADC2 pins (unusable while WiFi/ESP-NOW is active);
//     analog-capable pins are used as digital outputs only.

#ifndef PINS_CONFIG_H
#define PINS_CONFIG_H

// =========================
// SSR / Power Outputs
// =========================
// SSR1: PTC Heater (power stage)
// SSR2: Exhaust Outlet (220 V)
// SSR3: Inlet Air Fan
// SSR4: Exhaust Fan
#ifndef SSR1_PIN
#define SSR1_PIN 26
#endif

#ifndef SSR2_PIN
#define SSR2_PIN 25
#endif

#ifndef SSR3_PIN
#define SSR3_PIN 27
#endif

#ifndef SSR4_PIN
#define SSR4_PIN 13
#endif

// =========================
// HX711 Load Cell
// =========================
// DOUT is on an input-only pin (safe — it is a read line).
// SCK must be a clean digital output.
#ifndef LOADCELL_DOUT_PIN
#define LOADCELL_DOUT_PIN 35
#endif

#ifndef LOADCELL_SCK_PIN
#define LOADCELL_SCK_PIN 32
#endif

// =========================
// Reserved Pins
// =========================
// SHT31 temperature/humidity sensor (I2C) — temperature feedback for PID control.
#ifndef SHT31_SDA_PIN
#define SHT31_SDA_PIN 21
#endif

#ifndef SHT31_SCL_PIN
#define SHT31_SCL_PIN 22
#endif

// Optional wired HMI fallback (UART2). WROOM-32 only — do NOT use on
// WROVER modules (GPIO 16/17 are PSRAM there). Default transport is ESP-NOW.
#ifndef HMI_UART_TX_PIN
#define HMI_UART_TX_PIN 17
#endif

#ifndef HMI_UART_RX_PIN
#define HMI_UART_RX_PIN 16
#endif

#endif // PINS_CONFIG_H
