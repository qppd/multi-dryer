# HMI User Guide

How to use the touchscreen HMI (ESP32-S3 + 7″ display, LVGL). The HMI is the
**face** of the Multi Dryer — the controller makes all control decisions; the
HMI sends commands over ESP-NOW and displays what the controller reports.

> Implementation: [`src/MultiDryerHMI/`](../../src/MultiDryerHMI/) ·
> Protocol: [`../api/espnow-protocol.md`](../api/espnow-protocol.md)

---

## 0. Screen Map

```
BOOT ──(auto)──► DASHBOARD ──┬──► CONTROL ──(back)──► DASHBOARD
                             ├──► ANALYTICS ──(back)─► DASHBOARD
                             ├──► DIAGNOSTICS ─(back)► DASHBOARD
                             └──► HOW TO USE  ─(back)► DASHBOARD
```

Navigation from the dashboard (bottom nav bar):
**⚙ Control · ☰ Analytics · ◉ Diag** — plus **HOW TO USE** from the
dashboard/control area.

---

## 1. Boot Screen

- Splash with the **Multi-Purpose Dryer** branding, subtitle *"Smart Drying System"*.
- Shows `Initializing...` while the HMI boots and sends its boot-time **TARE**
  to the controller, then auto-advances to the **Dashboard**.
- If the controller never answers, the dashboard shows stale/`--` values — see
  the [troubleshooting guide](troubleshooting.md) §1.

---

## 2. Dashboard (main screen)

Live overview, refreshed from the 1 Hz status packet (UI redraw every ~2 s):

| Area | Shows |
|---|---|
| State badge | **IDLE / DRYING / PAUSED / COMPLETE** |
| Temperature | Big current reading + **Target: XX °C** |
| Humidity | Current %RH |
| Weight | Current product weight (kg) |
| Water Loss | Progress bar + % |
| Relays | Heater / Fan / Exhaust indicator lights (real SSR state) |
| Elapsed | Runtime (controller-authoritative, pause-aware) |
| EDT | Estimated time to completion (`---` while estimating) |

### Bottom-bar buttons (state-driven)

| State | Buttons shown |
|---|---|
| IDLE | ▶ **START** (green) |
| DRYING | ■ **STOP** (red) + ⏸ **PAUSE** |
| PAUSED | ■ **STOP** only (resume lives on the Control screen) |
| COMPLETE | ▶ **START** (next session) |

> **START from IDLE** is a *quick start*: it sends the current target
> temperature + water-loss target, then `START_DRYING` (50 ms apart), so a
> session never starts with setpoint 0 or auto-complete off.
> **PAUSED shows no START** — pressing it would silently *restart the session
> fresh* and destroy the water-loss reference. Use **RESUME** on the Control
> screen instead.

---

## 3. Control Screen

### 3.1 Drying Preset (one-tap temperature)
| Preset | Setpoint |
|---|---|
| 🐟 **Tuyo** | 60 °C |
| 🐟 **Danggit** | 60 °C |
| 🦑 **Pusit** | 50 °C |
| **Others** | Custom (use the +/− buttons) |

### 3.2 Manual temperature row
`-5  −  +  +5` around the current setpoint — each tap sends
`SET_TEMPERATURE` immediately (and persists to NVS on the controller).

### 3.3 Target Water Loss
Slider (with % label) — sets the **auto-complete** target. The controller stops
the session when this % is reached. 0% = auto-complete off.

### 3.4 Drying Mode / session buttons
| Button | Sends | Visible when |
|---|---|---|
| ▶ **START** | `START_DRYING` | IDLE / COMPLETE |
| ⏸ **PAUSE** | `PAUSE_DRYING` (all SSRs off, session saved) | DRYING |
| ▶ **RESUME** | `RESUME_DRYING` (PID back on) | PAUSED |
| ■ **STOP** | `STOP_DRYING` (session cleared) | DRYING / PAUSED |

Buttons give **instant feedback** (optimistic state) and are corrected by the
next 1 Hz status packet — that's normal, not a bug.

---

## 4. Analytics Screen

Tabbed history charts (LVGL charts, fed from the status stream):
- **Temperature (°C)** — chamber temperature over time
- **Humidity (%RH)** — relative humidity over time
- **Weight (kg)** — product weight trend (sessions show the water-loss curve)

Data is accumulated while the HMI runs; it resets on HMI reboot (no
cross-reboot history storage yet).

---

## 5. Diagnostics Screen

### 5.1 Sensor Status / Power / I2C / System cards
Live status: SHT31 (temp/humidity, CRC-valid), load cell weight, PID state,
I2C device list, uptime, packet counters, and last status-packet time.

### 5.2 ⚙ CALIBRATE SCALE (guided 3-step wizard)
1. **Step 1** — Empty the scale and tap **TARE SCALE** (controller must be IDLE).
2. **Step 2** — Set the known reference weight with `− / +`.
3. **Step 3** — Place the weight, then tap **CALIBRATE**.

→ Full details: [`calibration-guide.md`](calibration-guide.md).

### 5.3 ⟳ RUN SENSOR TEST
Forces an immediate status packet (SHT31 + load cell read) — handy for
confirming the link and sensors live.

> ⚠️ TARE/CALIBRATE are **ignored while a session is active** — the controller
> gates them to IDLE. Press STOP first.

---

## 6. HOW TO USE (instruction guide)

A 10-step illustrated quick-reference (static cards): prepare → load tray →
tare → set temperature → set water-loss target → start → monitor → complete →
unload/store → clean & maintain.

---

## 7. Alerts (popups)

| Popup | Trigger | Buttons |
|---|---|---|
| ⚠️ Warning | Sensor loss / abnormal state (e.g. SHT31 absent) | **ACKNOWLEDGE** |
| 🎉 DRYING COMPLETE | Controller reaches COMPLETE while HMI is running | **OK** (shows water loss % + total time) |

---

## 8. Button Semantics Summary

| Button | Command | Controller effect |
|---|---|---|
| START | `START_DRYING` | New session: initial weight captured, PID on |
| STOP | `STOP_DRYING` | All SSRs off, session cleared |
| PAUSE | `PAUSE_DRYING` | All SSRs off, session saved (resumable, even across reboot) |
| RESUME | `RESUME_DRYING` | PID back on, runtime continues |
| +/−, presets | `SET_TEMPERATURE` | Setpoint set + persisted |
| Slider | `SET_WATER_LOSS` | Auto-complete target set + persisted |
| TARE / CALIBRATE | `TARE_SCALE` / `CALIBRATE_SCALE` | Only in IDLE; factor saved to NVS |

---

## 9. First-Use Workflow (recommended)

1. **Load** the product on the tray; close the chamber door.
2. **Tare** (Diagnostics → TARE SCALE) with the product loaded.
3. **Configure** (Control): pick a preset or set temperature + water-loss target.
4. **Start** (Dashboard or Control) — watch temperature rise to setpoint.
5. **Monitor** — dashboard shows temp/humidity/weight/water loss/EDT.
6. **Completion** — the dryer stops itself; popup shows the result.
7. **Unload & clean** per the HOW TO USE steps.

---

## 10. Notes & Known Gaps

- **Manual heater/fan/exhaust control:** available on the Control screen —
  switching to **Manual mode** reveals Heater / Inlet Fan / Exhaust switches
  (`sendHeaterControl` / `sendFanControl` / `sendExhaustControl`). Any manual
  command ends an active session first, so manual and automatic control never fight.
- **History** resets on HMI reboot (analytics buffer is RAM-only).
- **Elapsed/EDT** come from the controller — if the HMI reboots mid-session, the
  numbers are correct again within 1 s (no local clock guessing).
