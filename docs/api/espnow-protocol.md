# ESP-NOW Protocol Reference (Controller ⇄ HMI)

The two firmware halves communicate **exclusively over ESP-NOW** — a wireless,
low-latency link built into the ESP32. There is **no wired connection** (no UART,
no I2C) between the controller and the HMI.

- **Shared header:** `espnow_protocol.h` — **byte-identical** in
  `src/MultiDryerController/` and `src/MultiDryerHMI/`. Keep both copies in sync
  when changing the protocol.
- **Wi-Fi channel:** `ESPNOW_WIFI_CHANNEL` = 1 on both sides.
- **Mode:** both boards run `WiFi.mode(WIFI_STA)` (no network connection needed).

---

## 1. Packet Framing

Both packets are packed C structs (little-endian, ESP32 Xtensa — binary-compatible
between boards). Every packet ends with a **checksum**: the XOR of all preceding
bytes. A receiver drops any packet whose type or checksum doesn't validate.

```
| packetType | payload ... | checksum |
```

| Packet | Direction | Size | ID |
|---|---|---|---|
| `EspNowStatusPacket` | Controller → HMI | **38 bytes** | `0x01` |
| `EspNowCmdPacket` | HMI → Controller | **7 bytes** | `0x02` |

---

## 2. EspNowStatusPacket — Controller → HMI (38 bytes)

Broadcast at **1 Hz** while the controller is running, and on demand after
`CMD_STATUS_REQUEST` / `CMD_SENSOR_TEST`. This is the HMI's only source of truth
for the machine state.

| Field | Type | Bytes | Description |
|---|---|---|---|
| `packetType` | uint8_t | 1 | `ESPNOW_PKT_STATUS` (0x01) |
| `state` | uint8_t | 1 | `DSTATE_*` — see [§4](#4-dryer-states) |
| `flags` | uint8_t | 1 | `FLAG_*` bitmask — see [§5](#5-status-flags) |
| `temperature` | float | 4 | Chamber temperature, °C (SHT31) |
| `humidity` | float | 4 | Humidity, % (SHT31) |
| `weight` | float | 4 | Current product weight, kg (HX711, 1 Hz cache) |
| `waterLoss` | float | 4 | Water lost so far, % of initial weight |
| `setpoint` | float | 4 | Target temperature, °C |
| `waterLossTarget` | float | 4 | Auto-complete target, % (0 = disabled) |
| `pidOutput` | float | 4 | PID output, 0–5000 |
| `runtimeSeconds` | uint16_t | 2 | Drying runtime (pauses excluded), s — wraps ~18 h |
| `estimatedEDT` | uint32_t | 4 | Estimated time to completion, s (0 = unknown) |
| `checksum` | uint8_t | 1 | XOR of bytes [0..36] |

> `runtimeSeconds` and `estimatedEDT` are authoritative from the controller's
> state machine (`DRYER_STATE.h`) — the HMI must display them as-is, not
> recompute from its own clock.

---

## 3. EspNowCmdPacket — HMI → Controller (7 bytes)

| Field | Type | Bytes | Description |
|---|---|---|---|
| `packetType` | uint8_t | 1 | `ESPNOW_PKT_CMD` (0x02) |
| `cmdType` | uint8_t | 1 | `CMD_*` — see [§3.1](#31-command-reference) |
| `value` | float | 4 | Optional parameter (0.0 when unused) |
| `checksum` | uint8_t | 1 | XOR of bytes [0..5] |

### 3.1 Command Reference

| ID | Command | Value | Effect on controller |
|---|---|---|---|
| 0x01 | `CMD_SET_TEMPERATURE` | target °C | Sets the PID setpoint; persists to NVS |
| 0x02 | `CMD_START_DRYING` | — | Starts a fresh drying session (captures initial weight, PID on) |
| 0x03 | `CMD_STOP_DRYING` | — | Ends the session (all SSRs off, session cleared) |
| 0x04 | `CMD_PAUSE_DRYING` | — | Pauses: all SSRs off, session saved (resumable) |
| 0x05 | `CMD_RESUME_DRYING` | — | Resumes a paused session (PID back on) |
| 0x10 | `CMD_HEATER_ON` | — | Manual: PTC heater + inlet fan on (ends any session first) |
| 0x11 | `CMD_HEATER_OFF` | — | Manual: stops session/all outputs |
| 0x12 | `CMD_FAN_ON` | — | Manual: inlet fan on (ends any session first) |
| 0x13 | `CMD_FAN_OFF` | — | Manual: stops session/all outputs |
| 0x14 | `CMD_EXHAUST_ON` | — | Manual: exhaust fan on (vents the chamber) |
| 0x15 | `CMD_EXHAUST_OFF` | — | Manual: stops session/all outputs |
| 0x20 | `CMD_PID_START` | — | Alias of `CMD_START_DRYING` (FishDryer baseline) |
| 0x21 | `CMD_PID_STOP` | — | Alias of `CMD_STOP_DRYING` |
| 0x22 | `CMD_SET_WATER_LOSS` | target % | Sets the auto-complete target; persists to NVS |
| 0x30 | `CMD_STATUS_REQUEST` | — | Forces an immediate status packet |
| 0x40 | `CMD_TARE_SCALE` | — | Tares the load cell (**blocked unless IDLE**) |
| 0x41 | `CMD_CALIBRATE_SCALE` | known weight kg | Two-point calibration (**blocked unless IDLE**) |
| 0x50 | `CMD_SENSOR_TEST` | — | Forces an immediate status packet (sensor check) |

> **TARE/CALIBRATE are gated to the IDLE state.** The HMI sends a TARE at every
> boot; if a session is active (or was restored from NVS), the TARE is ignored so
> it can't destroy the restored zero reference.

---

## 4. Dryer States

| Value | State | Meaning |
|---|---|---|
| 0 | `DSTATE_IDLE` | No session; manual mode available |
| 1 | `DSTATE_DRYING` | Active session; PID regulating the heater |
| 2 | `DSTATE_COMPLETE` | Target water loss reached; all outputs off |
| 3 | `DSTATE_PAUSED` | Session paused; all outputs off, resumable |

Transitions: `IDLE →START→ DRYING ⇄PAUSE/RESUME⇄ PAUSED`, `DRYING →COMPLETE` (auto
when the water-loss target is reached), any → `IDLE` via STOP.

---

## 5. Status Flags

Bitmask in `EspNowStatusPacket.flags` — reflects the **real SSR output state**.

| Bit | Flag | Set when |
|---|---|---|
| 0x01 | `FLAG_HEATER` | PTC heater SSR is energized |
| 0x02 | `FLAG_FAN` | Inlet fan SSR is energized |
| 0x04 | `FLAG_EXHAUST` | Exhaust SSR(s) energized |
| 0x08 | `FLAG_PID` | PID is in AUTOMATIC mode |
| 0x10 | `FLAG_SHT31` | SHT31 sensor reporting valid data |

---

## 6. Message Flow & Reliability

1. **Controller → HMI:** status packet every 1 s (`espnowUpdate()` → `sendStatusNow()`).
2. **HMI → Controller:** commands via `serial_protocol.cpp` senders; the controller
   queues them in an **8-slot ring buffer** (`cmdQueue*` in `espnow_link.h`) and
   drains one per loop — bursts (e.g. `SET_TEMPERATURE → SET_WATER_LOSS →
   START_DRYING`) are never dropped, even during the ~300 ms HX711 read window.
3. **Delivery:** ESP-NOW ACK updates the controller's `hmiReachable` flag; send
   failures are logged (`check CONTROLLER_PEER_MAC`).
4. **HMI UI:** buttons use optimistic state immediately, corrected by the next
   1 Hz status packet.

---

## 7. Pairing (one-time)

1. Boot the controller → note its MAC (`[ESP-NOW] Controller MAC: …`).
2. Paste it into `CONTROLLER_PEER_MAC` in `src/MultiDryerHMI/serial_protocol.cpp`.
3. Boot the HMI → note its MAC.
4. Paste it into `HMI_PEER_MAC` in `src/MultiDryerController/espnow_link.h`, re-flash.

No packets flow until both MACs are configured. Both must be on channel 1.

---

## 8. Extending the Protocol

1. Edit **both** copies of `espnow_protocol.h` identically (byte-identical code).
2. Add the command ID / field in the middle (not at the end) only if you also
   bump the checksum coverage — simplest is to append fields and keep the
   `checksum` as the last byte.
3. Add the handler in `espnow_link.h::handleCmd()` (controller) and the sender +
   `applyStatusPacket` mapping in the HMI (`serial_protocol.cpp`).
4. Version note: both structs are fixed-size; changing `sizeof` breaks older
   firmware — re-flash both boards together.
