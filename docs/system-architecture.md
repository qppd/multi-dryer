# System Architecture

High-level architecture of the Multi Dryer: two ESP32 boards, one shared
wireless protocol, and the module/data-flow design of each firmware.

> Diagrams: [`diagrams/block-diagram.md`](diagrams/block-diagram.md) ·
> [`diagrams/flow-chart.md`](diagrams/flow-chart.md) ·
> Protocol: [`api/espnow-protocol.md`](api/espnow-protocol.md) ·
> Stack: [`stacks/tech-stack.md`](stacks/tech-stack.md)

---

## 1. Overview

The system is a **two-board distributed design** with a strict separation of
concerns:

```
┌──────────────────────────────┐        ESP-NOW (wireless)        ┌──────────────────────────────┐
│        CONTROLLER            │ ◄══════════════════════════════► │             HMI              │
│   ESP32 38-pin (WROOM-32)    │    status 1 Hz / commands        │   ESP32-S3 + 7" touch LCD   │
│                              │                                  │                              │
│  sensing · control · safety  │                                  │  display · input · state     │
└──────────────────────────────┘                                  └──────────────────────────────┘
```

- The **controller** is the brain: it owns the sensors, the PID, the drying
  state machine, and the safety rules. It never needs the HMI to operate safely.
- The **HMI** is the face: LVGL touchscreen UI, optimistic button feedback, and
  display of controller data. It never makes control decisions.
- The only link is **ESP-NOW** — no UART, no I2C between boards. Each board's
  serial (UART0) is local flashing/debug only.

**Why two boards?** The 38-pin ESP32 runs the real-time control loop with clean
GPIOs for the SSRs and sensors; the ESP32-S3 runs the display without sharing
the controller's I/O. ESP-NOW keeps them physically independent (no wiring, no
ground loops in the mains enclosure).

---

## 2. Controller Architecture

Modules in `src/MultiDryerController/`, roughly in init order:

| Module | Responsibility |
|---|---|
| `MultiDryerController.ino` | Entry point: init sequence (pins → SHT31 → load cell → PID → drying → ESP-NOW) and the main loop |
| `PINS_CONFIG.h` | Boot/WiFi-safe pin assignments (single source of truth) |
| `SHT31_CONFIG.h` | Non-blocking SHT31 driver (I2C, CRC-8) → `getTemperature()` / `getHumidity()` |
| `LOADCELL_CONFIG.h` | HX711 driver: kg reads, TARE/CALIBRATE, factor in NVS |
| `PID_CONFIG.h` | PID_v1 temperature control + **vent safety guard** |
| `DRYER_STATE.h` | State machine, water-loss math, runtime/EDT, NVS session & config persistence |
| `espnow_link.h` | ESP-NOW: 1 Hz status broadcast, 8-slot command ring buffer |
| `espnow_protocol.h` | Shared packet protocol (byte-identical copy) |

### 2.1 Data flow (per loop iteration)

```
SHT31 ──► temperature/humidity ─┐
                                ├─► PID (PID_CONFIG) ─► SSRs (heat/vent)
HX711 ──► weight (1 Hz cache) ─┤        ▲ setpoint
                                └─► DRYER_STATE (state machine, water loss)
                                        │
espnow_link ◄── status packet (1 Hz) ───┘   commands ──► state machine / PID / manual
```

### 2.2 Safety architecture

1. **Boot fail-safe** — firmware forces all SSR pins LOW as the first step
   of `setup()`: nothing energizes during boot.
2. **Sensor-fail vent guard** — if `sht31OK` is false, `pidCOMPUTE()` forces
   heater + inlet fan OFF and vents (exhaust fan on): a dead sensor can
   never cause full-power heating.
3. **State-machine/manual arbitration** — any manual command ends an active
   session first, so PID and manual control never fight.
4. **TARE/CALIBRATE gate** — calibration is blocked unless IDLE, so a boot-time
   TARE can't destroy a restored session's zero reference.
5. **Hardware thermal cutoff** — thermal fuse/limit switch in series with the
   PTC heater branch (outside firmware).

---

## 3. HMI Architecture

Modules in `src/MultiDryerHMI/`:

| Module | Responsibility |
|---|---|
| `MultiDryerHMI.ino` | Entry point |
| `serial_protocol.*` | ESP-NOW transport: senders + `applyStatusPacket` (packet → `dryerData`) |
| `dryer_data.h` | Data model — the single source the screens read |
| `screen_manager.*` | Screen navigation |
| `boot_screen` / `dashboard` / `control` / `manual_operation` / `analytics` / `diagnostics` / `alert_popup` | LVGL screens |
| `ui_optimistic_state.*` | Instant UI feedback on button press, corrected by the next 1 Hz status |
| `lvgl_v8_port.*`, `ui_theme.h`, `ui_styles.*` | LVGL port, theme, styles |
| `esp_panel_board_custom_conf.h` | Display/touch board config (Waveshare-7″) |

### 3.1 UI data flow

```
ESP-NOW status ──► applyStatusPacket ──► dryerData ──► screens (2 s UI refresh)
ESP-NOW command ◄── button callbacks ◄── optimistic state ── touch input
```

---

## 4. Communication Architecture

- **Transport:** ESP-NOW over Wi-Fi STA, channel 1, no network connection.
- **Packets:** packed binary structs + XOR checksum (see `espnow_protocol.h`).
  - `EspNowStatusPacket` (38 B) — controller → HMI, 1 Hz.
  - `EspNowCmdPacket` (7 B) — HMI → controller, on demand.
- **Reliability:** status is periodic (a missed packet self-heals); commands are
  queued in an 8-slot ring buffer so bursts are never dropped during the
  ~300 ms HX711 read window. ESP-NOW ACK tracks `hmiReachable`.
- **Authoritative data:** runtime, EDT, water loss, and weight always come from
  the controller — the HMI never recomputes them.

---

## 5. Persistence Architecture (NVS)

| Namespace | Keys | Contents | Written by |
|---|---|---|---|
| `loadcell` | `factor` | Calibration factor (counts/kg) | `CALIBRATE:<kg>` |
| `drying` | `magic`, `state`, `initW`, `runMs`, `wlTgt`, `setpt`, `hxoff` | Active session: state, initial weight, elapsed runtime, water-loss target, setpoint, **HX711 raw offset** | session transitions + every 30 s while drying |
| `drying` | `magic`, `wlTgt`, `setpt` | Idle config (survives reboot with no session) | `SET_TEMPERATURE` / `SET_WATER_LOSS` |

Restore rules: valid `magic` + non-IDLE `state` → resume session (re-apply
offset, PID back on if DRYING). Config keys restore in every boot. Session
completion/stop clears the state marker only — config keys are preserved.

---

## 6. Key Design Decisions

| Decision | Rationale |
|---|---|
| ESP-NOW instead of wires | No inter-board wiring, no ground loops; same transport as the HMI reference baseline |
| Controller owns all safety | The dryer remains safe even if the HMI is dead or absent |
| 1 Hz weight cache in the state machine | Single read source → status packet and water-loss math always agree |
| NVS offset saved with the session | Survives full power cycles where HX711 offset drifts — no re-tare |
| Optimistic HMI UI | Feels instant; the 1 Hz status packet is the correction source |
| Boot-safe pin map | No strapping/flash/ADC2 pins → flashing and WiFi always work with peripherals attached |

## 7. References

- Wiring: [`schematic/hardware-wiring.md`](schematic/hardware-wiring.md)
- Protocol: [`api/espnow-protocol.md`](api/espnow-protocol.md)
- Stack: [`stacks/tech-stack.md`](stacks/tech-stack.md)
- Bring-up: [`guides/bring-up-checklist.md`](guides/bring-up-checklist.md)
- Troubleshooting: [`guides/troubleshooting.md`](guides/troubleshooting.md)
