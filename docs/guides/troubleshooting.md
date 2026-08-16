# Troubleshooting Guide

Systematic fixes for the most common Multi Dryer problems. Each section follows
**Symptom → Likely cause → Fix**, and ends with how to diagnose it.

> Related: [`bring-up-checklist.md`](bring-up-checklist.md) (ordered verification),
> [`calibration-guide.md`](calibration-guide.md) (load cell),
> [`../api/espnow-protocol.md`](../api/espnow-protocol.md) (packets & commands).

---

## 1. Communication — MAC Pairing & No Status Packets

### 1.1 No packets flow between controller and HMI

| Symptom | Likely cause | Fix |
|---|---|---|
| HMI dashboard stuck on `--` / stale values | Peer MACs are still the zero placeholders `{0x00,…}` | Complete the 4-step pairing (§1.2) |
| Controller logs `Add peer FAILED — is HMI_PEER_MAC set?` | `HMI_PEER_MAC` (controller `espnow_link.h`) not pasted | Paste the HMI's MAC, re-flash the controller |
| Controller logs `[ESP-NOW] Send failed — check CONTROLLER_PEER_MAC` | `CONTROLLER_PEER_MAC` (HMI `serial_protocol.cpp`) not pasted | Paste the controller's MAC, re-flash the HMI |
| Both MACs set, still nothing | Channel mismatch | Both must use `ESPNOW_WIFI_CHANNEL` (default 1) |
| Works at bench, fails in the field | ESP-NOW is same-channel-only; both boards on a network's other channel | Keep both off Wi-Fi or set the channel to match |

### 1.2 The pairing procedure (one-time)

1. Boot the controller → read `[ESP-NOW] Controller MAC: XX:…`.
2. Paste it into `CONTROLLER_PEER_MAC` in `src/MultiDryerHMI/serial_protocol.cpp`.
3. Boot the HMI → read its MAC.
4. Paste it into `HMI_PEER_MAC` in `src/MultiDryerController/espnow_link.h`, re-flash.

**Diagnose:** both serial monitors should show clean sends. The controller's
`hmiReachable` flag turns true when the HMI ACKs. Expect a `[STATUS]`/status
packet **every 1 s** once paired.

### 1.3 Status packets arrive but the HMI shows stale values

- **Cause:** the HMI's `applyStatusPacket` only updates on *valid* packets
  (type + checksum). A corrupted/truncated packet is dropped silently.
- **Fix:** check the shared `espnow_protocol.h` is **byte-identical** in both
  folders — if the copies drifted, struct sizes mismatch and every packet is
  rejected.

---

## 2. Sensor-Fail Vent Behavior (Safety)

This is **intended behavior**, not a bug: if the SHT31 temperature feedback is
lost, the heater can never run at full power.

| Observation | Meaning | Action |
|---|---|---|
| `FLAG_SHT31` clears on the HMI | SHT31 not reporting valid data | Check I2C wiring (SDA→21, SCL→22), pull-ups, address (0x44 / 0x45) |
| Heater + inlet fan off, **exhaust fan on** while in DRYING | Vent guard active — `pidCOMPUTE()` sees `!sht31OK` | Fix the sensor; the unit is intentionally venting instead of heating |
| `[STATUS]` shows `temp=0` or stale temp | Sensor read failing or hung bus | Re-seat the SHT31; power-cycle the controller |
| Serial shows CRC errors | Bad wiring / noise on I2C | Shorten leads, keep I2C wiring away from AC wiring |

> **Rule:** a dead sensor must never cause full-power heating. If you see the
> heater ON with no valid temperature, something is wrong with the vent-guard
> logic — report it.

---

## 3. Controller Won't Boot / Flashing Fails

| Symptom | Likely cause | Fix |
|---|---|---|
| Board stuck in download mode / boot loop | A strapping pin is loaded down (0, 2, 4, 5, 12, 15) | Check wiring on strapping pins; remove external pull-ups |
| SSR clicks / pulses at power-on | GPIO 14 used as an output (PWM burst at boot) | Move to a clean GPIO; never use GPIO 14 |
| Upload fails with peripherals attached | A pin on the flash (6–11) or UART0 (1/3) line is loaded | Free GPIO 1/3 for USB-serial; keep 6–11 untouched |
| Random resets during operation | Brown-out (5 V rail too weak) or WiFi current spikes | 12 V→5 V buck ≥ 3 A; add bulk capacitance on the 5 V rail |

**Diagnose:** open the serial monitor at 115200 — the boot banner order is
pins → SHT31 → load cell → PID → drying → ESP-NOW. Whichever line is missing/
crashing identifies the failing module.

---

## 4. Load Cell / Weight

| Symptom | Likely cause | Fix |
|---|---|---|
| `WARNING: HX711 not responding!` | Wiring / power / DOUT floating | Check DOUT=35, SCK=32, VCC, GND; HX711 must be powered before `is_ready()` |
| Weight reads 0.000 always | Sensor not ready → `readLoadCell()` returns 0 | Check wiring; watch for the transition-only "not ready" print |
| Weight is negative | Noise clamp or wrong factor sign | `readLoadCell()` clamps negatives to 0; re-calibrate |
| TARE/CALIBRATE "ignored" | **Session active** — commands gated to IDLE | Press STOP, then TARE/CALIBRATE |
| Water loss corrupt after reboot mid-drying | A TARE ran during the session (destroyed reference) | Shouldn't happen (gated); if it did, the firmware was older or NVS was cleared |
| Weight jumps with the heater on | AC noise coupling into the HX711 | Keep load cell leads twisted & away from AC; add 100 nF on HX711 VCC |

Full procedure: [`calibration-guide.md`](calibration-guide.md).

---

## 5. Session / NVS Persistence

| Symptom | Likely cause | Fix |
|---|---|---|
| Completed session "resurrects" as DRYING after reboot | NVS marker left non-IDLE (old firmware wrote `state` in config persist) | Press STOP after COMPLETE; on current firmware this can't happen |
| Setpoint/water-loss target reset after reboot | Config never persisted | Send `SET_TEMPERATURE` / `SET_WATER_LOSS` once — values persist from then on |
| Mid-session reboot loses up to 30 s of runtime | Periodic save cadence (`DRYER_SAVE_INTERVAL_MS` = 30 s) | Expected by design; nothing to fix |
| Restored session weight looks wrong | HX711 offset drifted between save and boot | The saved raw offset is re-applied on restore; if still off, verify the load cell mechanically |
| NVS corrupted / want a factory reset | Stale or bad saved state | Clear NVS: `Preferences` namespace `drying` (session/config) and `loadcell` (factor); erase flash in the IDE then re-flash |

---

## 6. HMI Display / Touch

| Symptom | Likely cause | Fix |
|---|---|---|
| Display stays black / splash only | Board config mismatch | `esp_panel_board_custom_conf.h` is **Waveshare ESP32-S3-Touch-LCD-7** specific — verify against your actual board, or display/touch init fails silently |
| Touch unresponsive but display works | GT911 config wrong for your panel | Check the touch I2C pins / reset in the board config |
| UI janky after a button press | Blocking `delay()` in an LVGL callback | Keep callbacks short (the dashboard quick-start uses small 50 ms gaps by design) |

---

## 7. PID / Temperature

| Symptom | Likely cause | Fix |
|---|---|---|
| Heater never comes on | Setpoint ≤ 0, or SHT31 lost (vent guard) | Send `SET_TEMPERATURE`; check `FLAG_SHT31` |
| Big overshoot / slow to reach setpoint | Tuning off for your chamber | Adjust KP/KD in `PID_CONFIG.h` (start KP 4.0 / KD 22.0); log `pidOutput` |
| Temperature oscillates | Sample time vs. sensor cadence mismatch | `pid.SetSampleTime` = `SHT31_READ_INTERVAL_MS` (2 s) — keep them matched |
| `FLAG_PID` set but outputs don't change | PID in AUTOMATIC but output 0 = venting | Normal when above setpoint — unit vents below target |

---

## 8. Collecting a Diagnostic Snapshot

Before reporting an issue, capture:

1. **Controller serial** (115200) — last 100 lines: boot banner, `[STATUS]` lines,
   `[ESP-NOW]` errors, `[DRYER]` transitions.
2. **HMI serial** — pairing prints, send failures.
3. **HMI dashboard state** — current `DSTATE`, flags, temp/humidity/weight.
4. **What changed** — new wiring, new firmware, mechanical change, power event.

**Status packet quick decode** (from the diagnostics screen or serial):
- `state`: 0 IDLE · 1 DRYING · 2 COMPLETE · 3 PAUSED
- `flags`: 0x01 heater · 0x02 fan · 0x04 exhaust · 0x08 PID · 0x10 SHT31
- `estimatedEDT = 0` → still estimating, not an error.
