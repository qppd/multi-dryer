# Technology Stack

Everything used to build the Multi Dryer system — hardware, firmware, libraries,
and tooling.

---

## 1. Hardware Stack

| Layer | Item | Role |
|---|---|---|
| Controller MCU | **ESP32 38-pin DevKitC** (WROOM-32 module) | Runs the drying process: sensors, PID, state machine, ESP-NOW |
| HMI MCU | **ESP32-S3** (Waveshare ESP32-S3-Touch-LCD-7) | 800×480 RGB LCD + GT911 capacitive touch, runs the LVGL UI |
| Temperature/humidity | **SHT31** (I2C, addr 0x44) | Chamber feedback for PID + humidity reporting |
| Weight | **HX711** 24-bit ADC + 4-wire load cell (1–50 kg) | Product weight → water-loss % |
| Heating | **PTC heater** (500–2000 W typical) | Drying heat, switched by SSR1 |
| Airflow | Inlet fan + exhaust fan (220 V AC) + exhaust outlet | Circulation & moisture venting, SSRs 2–4 |
| Power switching | **4× SSR** (3–32 VDC input / 220 VAC output) | Opto-isolated mains switching; firmware-enforced fail-safe OFF at boot |
| Power | 5 V isolated PSU ≥ 3 A (e.g. Hi-Link HLK-PM03) | Low-voltage rail (ESP32's onboard regulator makes 3.3 V) |
| Protection | Fuses per AC branch + thermal cutoff on heater branch | Safety |

Pin allocation: `src/MultiDryerController/PINS_CONFIG.h` · wiring plan:
`docs/schematic/wiring-diagram.md`

---

## 2. Firmware Stack

### Controller — `src/MultiDryerController` (Arduino sketch + headers)

| Module | Responsibility |
|---|---|
| `MultiDryerController.ino` | Entry point: init order (pins → SHT31 → load cell → PID → drying → ESP-NOW), main loop |
| `PINS_CONFIG.h` | Boot/WiFi-safe pin assignments |
| `SHT31_CONFIG.h` | Non-blocking SHT31 driver, hardware I2C, CRC-8 |
| `LOADCELL_CONFIG.h` | HX711 driver: kg readings, TARE/CALIBRATE, calibration factor in NVS |
| `PID_CONFIG.h` | PID temperature control (PID_v1) + SHT31 vent safety guard |
| `DRYER_STATE.h` | State machine, water-loss math, runtime/EDT, NVS session + config persistence |
| `espnow_link.h` | ESP-NOW transport: 1 Hz status, 8-slot command ring buffer |
| `espnow_protocol.h` | Shared protocol (byte-identical copy) |

### HMI — `src/MultiDryerHMI` (Arduino sketch, LVGL v8)

| Module | Responsibility |
|---|---|
| `MultiDryerHMI.ino` | Entry point |
| `boot_screen` / `dashboard_screen` / `control_screen` / `manual_operation_screen` / `analytics_screen` / `diagnostics_screen` / `alert_popup` | UI screens |
| `serial_protocol.*` | ESP-NOW transport to the controller |
| `dryer_data.h` | Data model bridged from status packets |
| `ui_optimistic_state.*` | Instant button feedback, corrected by 1 Hz status |
| `lvgl_v8_port.*`, `ui_theme.h`, `ui_styles.*`, `screen_manager.*` | LVGL port, theme, screen navigation |
| `esp_panel_board_custom_conf.h` | Board config — **Waveshare-7″-specific, verify against real hardware** |

---

## 3. Libraries

| Library | Used by | Version / notes |
|---|---|---|
| **ESP32 Arduino core** | both | `WiFi`, `Preferences`, `Wire` (built-in) |
| **PID_v1** (`br3ttb/Arduino-PID-Library`) | controller | PD tuning: KP 4.0, KI 0.0, KD 22.0 |
| **HX711** (`bogde/HX711`) | controller | Reads + tare + offset management |
| **LVGL 8.x** | HMI | Fonts Montserrat 14/16/20/24/30/36/48; widgets meter/chart/bar/slider/switch/tabview/checkbox/spinner |
| **ESP32_Display_Panel** (`esp_display_panel.hpp`) | HMI | RGB LCD + GT911 touch init (Waveshare board config) |

---

## 4. Communication

- **Transport:** ESP-NOW (Wi-Fi STA mode, channel 1) — wireless, no wiring between boards.
- **Protocol:** packed binary structs + XOR checksum, defined in `espnow_protocol.h`
  (byte-identical on both sides). Full reference: `docs/api/espnow-protocol.md`.
- **Cadence:** status 1 Hz; commands on-demand with an 8-slot receive queue.
- **Persistence:** NVS (`Preferences`, namespace `drying`) for calibration factor,
  session state, and last setpoint/water-loss target.

---

## 5. Tooling

| Tool | Use |
|---|---|
| Arduino IDE (or PlatformIO) | Build & flash both sketches |
| esptool / Arduino uploader | Flashing via UART0 (GPIO 1/3) |
| Serial monitor | MAC pairing, debug prints |
| Cirkit Designer | Interactive wiring schematic (link in `docs/schematic/wiring-diagram.md`) |
| GitHub (qppd/multi-dryer) | Version control, docs, issues |
| PID_v1 / HX711 / LVGL docs | Library API references |

---

## 6. Design Principles (why the stack is safe)

1. **Boot-safe pins** — no strapping (0/2/4/5/12/15), flash (6–11), or boot-PWM (14) pins.
2. **WiFi-safe pins** — no analog reads on ADC2 (unusable with WiFi active); HX711 path is fully digital.
3. **Fail-safe outputs** — firmware forces all SSR pins LOW first thing in `setup()`.
4. **Sensor-fail safety** — SHT31 loss forces the heater OFF and vents instead.
5. **Single transport** — ESP-NOW only between boards; UART0 is local debug only.
