# Multi Dryer

![Language](https://img.shields.io/badge/language-C%2B%2B-blue)
![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20ESP32--S3-green)
![Transport](https://img.shields.io/badge/transport-ESP--NOW-orange)
![Status](https://img.shields.io/badge/status-hardware%20bring--up-lightgrey)
![License](https://img.shields.io/badge/license-MIT-green)

Smart food dehydration system built on a pair of ESP32 boards: a **controller** that runs the drying process (PTC heater, fans, load cell, temperature sensing) and a **touchscreen HMI** that provides the user interface. The two halves communicate **wirelessly over ESP-NOW** — no wiring between boards.

## Features

- **PID temperature control** — PTC heater regulated from SHT31 feedback (PD tuning, 0–5000 output, heat-vs-vent SSR strategy). Safety guard: the heater is forced OFF and the unit vents if the sensor feedback is lost.
- **Water-loss tracking** — load cell (HX711) measures the product weight; water loss is computed as % of the initial weight, with a configurable auto-complete target.
- **Drying state machine** — `IDLE → DRYING ⇄ PAUSED → COMPLETE`, with pause-aware runtime accounting and a rate-based *estimated drying time* (EDT).
- **Session persistence (NVS)** — interrupted drying sessions survive reboot/power-cycle: initial weight + HX711 zero reference, elapsed runtime, and targets are restored automatically (DRYING re-enables the PID). The last setpoint and water-loss target persist even when idle.
- **Full HMI control** — LVGL v8 touchscreen UI: dashboard, control, manual operation, analytics, diagnostics screens + completion alerts. Start / stop / pause / resume, setpoint and water-loss presets, live weight, water loss, runtime and EDT.
- **Manual overrides** — heater / inlet fan / exhaust toggles; any manual command ends an active session first so manual and automatic control never fight.
- **Boot- and WiFi-safe pin map** — no strapping, flash, or ADC2 pins used; fail-safe SSR pull-downs (all outputs OFF at power-on).

## Repository Structure

```
multi-dryer/
├── src/
│   ├── MultiDryerController/   # ESP32 38-pin controller firmware (Arduino .ino + headers)
│   │   ├── MultiDryerController.ino   # entry point: sensors → PID → state machine → ESP-NOW
│   │   ├── PINS_CONFIG.h              # centralized, boot/WiFi-safe pin assignments
│   │   ├── SHT31_CONFIG.h             # SHT31 temp/humidity driver (I2C, non-blocking, CRC-8)
│   │   ├── LOADCELL_CONFIG.h          # HX711 load cell driver (kg, TARE/CALIBRATE, NVS factor)
│   │   ├── PID_CONFIG.h               # PID temperature control (PID_v1) + SHT31 vent guard
│   │   ├── DRYER_STATE.h              # state machine, water-loss math, NVS session persistence
│   │   ├── espnow_link.h              # ESP-NOW transport (1 Hz status, command ring buffer)
│   │   └── espnow_protocol.h          # shared packet protocol (byte-identical in both halves)
│   └── MultiDryerHMI/          # ESP32-S3 touchscreen HMI firmware (LVGL v8)
│       ├── MultiDryerHMI.ino          # entry point
│       ├── *_screen.cpp/.h            # boot, dashboard, control, manual operation,
│       │                              # analytics, diagnostics + alert popup
│       ├── serial_protocol.cpp/.h     # ESP-NOW transport to the controller
│       ├── dryer_data.h               # data model bridged from status packets
│       ├── lvgl_v8_port.* / ui_theme.h / ui_styles.* / screen_manager.*
│       ├── ui_optimistic_state.*      # instant UI feedback, corrected by 1 Hz status
│       └── esp_panel_board_custom_conf.h  # Waveshare Touch-LCD-7 board config (verify!)
└── docs/
    ├── README.md                      # documentation index & roadmap
    ├── api/espnow-protocol.md         # ESP-NOW protocol/API reference
    ├── stacks/tech-stack.md           # hardware + firmware + library stack
    ├── guides/bring-up-checklist.md   # ordered boot/hardware verification
    ├── schematic/hardware-wiring.md   # written pin-by-pin wiring plan
    ├── schematic/wiring-diagram.md    # interactive wiring diagram link (Cirkit Designer)
    └── diagrams/                      # (future) diagrams
```

## Documentation

- [docs/README.md](docs/README.md) — index & required-docs roadmap
- [ESP-NOW protocol / API](docs/api/espnow-protocol.md) — packets, commands, states, pairing
- [Technology stack](docs/stacks/tech-stack.md) — hardware, firmware, libraries, tools
- [Wiring diagram](docs/schematic/wiring-diagram.md) — interactive Cirkit Designer schematic
- [Hardware wiring plan](docs/schematic/hardware-wiring.md) — written pin-by-pin reference
- [Bring-up checklist](docs/guides/bring-up-checklist.md) — ordered boot/hardware verification
- [Calibration guide](docs/guides/calibration-guide.md) — TARE + CALIBRATE:<kg> procedure, NVS persistence

## Hardware

| Part | Spec / Use |
|---|---|
| ESP32 38-pin DevKitC (WROOM-32) | Main controller |
| ESP32-S3 + 7″ touch display (Waveshare Touch-LCD-7) | HMI (LVGL UI) |
| PTC heater + 220 V fans ×2, exhaust outlet | Drying loads, switched by SSRs |
| SSR modules ×4 (3–32 VDC input) | AC load switching, fail-safe pull-downs |
| HX711 + load cell (1–50 kg) | Weight / water-loss measurement |
| SHT31 | Temperature & humidity (PID feedback) |
| 5 V isolated PSU (≥3 A) | Low-voltage supply |

### Controller Pin Map (summary)

| Function | GPIO |
|---|---|
| SSR1 — PTC heater | 26 |
| SSR2 — exhaust outlet | 25 |
| SSR3 — inlet fan | 27 |
| SSR4 — exhaust fan | 13 |
| HX711 DOUT / SCK | 35 / 32 |
| SHT31 SDA / SCL | 21 / 22 |

Full wiring plan: [`docs/schematic/hardware-wiring.md`](docs/schematic/hardware-wiring.md) and the interactive [Cirkit Designer schematic](https://app.cirkitdesigner.com/project/9d03db5a-37c8-4f46-ac3f-7d5821ce26ce).

## Getting Started

### Prerequisites

**Controller** (`src/MultiDryerController`)
- Arduino IDE or PlatformIO with **ESP32 Arduino core**
- Libraries: [`br3ttb/PID_v1`](https://github.com/br3ttb/Arduino-PID-Library), [`bogde/HX711`](https://github.com/bogde/HX711) (`Preferences` is built-in)

**HMI** (`src/MultiDryerHMI`)
- **LVGL 8.x** (enable fonts *Montserrat 14/16/20/24/30/36/48* and widgets *meter/chart/bar/slider/switch/tabview/checkbox/spinner* in `lv_conf.h`)
- **ESP32_Display_Panel** library (`esp_display_panel.hpp`)
- ⚠️ `esp_panel_board_custom_conf.h` is configured for the **Waveshare ESP32-S3-Touch-LCD-7** — verify it matches your display/touch hardware, or init will fail silently.

### Flashing

1. Open the sketch folders (`src/MultiDryerController` / `src/MultiDryerHMI`) in the Arduino IDE, select the correct board, and upload.
2. **Pair the boards** (one-time, both default to channel 1):
   1. Boot the controller → note its MAC from the serial monitor.
   2. Paste it into `CONTROLLER_PEER_MAC` in `src/MultiDryerHMI/serial_protocol.cpp`.
   3. Boot the HMI → note its MAC.
   4. Paste it into `HMI_PEER_MAC` in `src/MultiDryerController/espnow_link.h`, re-flash the controller.
3. Open the serial monitors — status packets should flow at 1 Hz once paired.

## How It Works

1. The **controller** samples the SHT31 and load cell, runs PID temperature control on the PTC heater, and tracks the drying state machine with water-loss math.
2. Every second it broadcasts an **ESP-NOW status packet** (state, temp/humidity, weight, water loss, setpoint, PID output, runtime, EDT) to the HMI.
3. The **HMI** renders the dashboard and relays user commands (start/stop/pause/resume, setpoint, water-loss target, manual overrides, tare/calibrate) back over ESP-NOW. Commands are handled through an 8-slot ring buffer, so bursts are never dropped.
4. The drying session auto-completes when the water-loss target is reached — and survives reboots via NVS.

> The shared protocol (`espnow_protocol.h`) is **byte-identical** in both firmware folders — keep the two copies in sync when changing it.

## Author

**Sajed Lopez Mendoza** — *Building intelligent solutions* 🚀

- GitHub: [@qppd](https://github.com/qppd)
- LinkedIn: [sajed-mendoza](https://www.linkedin.com/in/sajed-mendoza)

## License

Distributed under the [MIT License](LICENSE).

