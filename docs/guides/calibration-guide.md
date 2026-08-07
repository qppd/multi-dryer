# Load Cell Calibration Guide

How to calibrate the HX711 + load cell on the Multi Dryer controller, and how
the calibration persists.

> Implementation: [`src/MultiDryerController/LOADCELL_CONFIG.h`](../../src/MultiDryerController/LOADCELL_CONFIG.h)
> (bogde/HX711 library, reads in kg, factor persisted in NVS).

---

## 1. Background — what gets calibrated

The HX711 reads raw counts from the load cell. Two values turn raw counts into
kilograms:

| Value | What it is | Where it lives |
|---|---|---|
| **Calibration factor** | Counts per kg (`scale.get_scale()`) — scales raw ADC counts to weight | NVS namespace `loadcell`, key `factor` |
| **Offset (zero reference)** | The raw count read with nothing on the scale (`scale.get_offset()`) | NVS namespace `drying`, key `hxoff` (saved with every drying session) |

- **Factor** is set by `CALIBRATE:<kg>` and restored automatically on boot.
- **Offset** is set by `TARE`. It is *also* captured with every drying session
  (with the initial weight), so a mid-drying reboot restores the exact zero
  reference — no re-tare, even after a power cycle.

> ⚠️ **TARE / CALIBRATE are only accepted while the state machine is IDLE.**
> If a session is active (or was restored), the commands are ignored — this
> protects the water-loss reference.

---

## 2. When to calibrate

- **First bring-up** — always (the default factor `-10697.956054f` is a starting point from the reference baseline, not your rig).
- **After any mechanical change** — moving the load cell, changing the tray/platform, re-mounting.
- **When readings drift** — weight reading off by a consistent percentage → recalibrate the factor; off by a consistent *amount* → re-tare.
- **Periodically** — every few weeks of use, or after any heavy load/shock to the cell.

---

## 3. Prerequisites

- [ ] Controller running, state machine **IDLE** (no session active — check the HMI dashboard shows IDLE).
- [ ] HX711 powered and connected (DOUT→GPIO 35, SCK→GPIO 32) — serial should say `Load cell ready (kg)`.
- [ ] A known-weight object (a verified 1 kg, 2 kg, or 5 kg weight; a digital kitchen scale to check anything else).
- [ ] Nothing resting on the scale, and the platform must be **free to move** (not touching the enclosure).

---

## 4. Procedure (recommended: from the HMI)

The HMI sends the same commands over ESP-NOW (`CMD_TARE_SCALE` /
`CMD_CALIBRATE_SCALE`); the diagnostics screen is the usual place.

### Step 1 — Tare (zero)
1. Make sure the scale is **empty** and the platform is stable.
2. Send **TARE** (HMI diagnostics button, or `CMD_TARE_SCALE`).
3. Expected serial: `Load cell tared. Place known weight, then send CALIBRATE:<kg>.`
4. Expected HMI weight: **0.000 kg** (give it ~1 s to settle).

### Step 2 — Place the known weight
1. Center the known weight on the platform.
2. Wait ~2 s for the reading to stabilize.
3. Confirm the HMI shows roughly the known weight (it won't be exact yet — that's what Step 3 fixes).

### Step 3 — Calibrate with the known mass
1. Send **CALIBRATE** with the known mass in kg (e.g. `CALIBRATE:2` for a 2 kg weight).
2. Expected serial:
   ```
   --- Calibration complete ---
   Factor <value> saved to NVS.
   ```
3. Expected HMI weight: the reading should now match the known weight within ±2%.

### Step 4 — Verify
1. Remove the weight → HMI returns to ~0.000 kg (re-tare if slightly off).
2. Put the weight back → reads the known mass.
3. Repeat once more — readings must be repeatable.

> **Alternative — serial only:** the FishDryer protocol accepts the same
> commands on the controller's serial line. If you're testing without the HMI,
> use the serial monitor instead. Both paths end at `tareLoadCell()` /
> `calibrateLoadCell()`, so the result is identical.

---

## 5. How the factor is stored

```cpp
// LOADCELL_CONFIG.h
Preferences prefs;
prefs.begin("loadcell", false);          // read-write
prefs.putFloat("factor", scale.get_scale());
prefs.end();
```

- Stored in **NVS** (wear-leveled, native ESP32 storage) — the Nano baseline's
  EEPROM equivalent.
- **Restored on boot** in `initLoadCell()` if the saved value is valid
  (`0 < factor < 999999` and finite); otherwise the default factor
  `-10697.956054f` is used and serial reminds you to calibrate.
- The factor is **not** affected by drying sessions (session NVS only stores
  the offset/zero reference).

---

## 6. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `WARNING: HX711 not responding!` at boot | Wiring / power / too little wait | Check DOUT=35, SCK=32, VCC=3V3/5V, GND; let it power-stabilize |
| Weight reads **negative** | Wrong factor sign, or clamped noise | Re-calibrate; `readLoadCell()` clamps negatives to 0 anyway |
| Reading off by a consistent **%** | Stale factor (e.g. after platform change) | Re-run CALIBRATE with a known weight |
| Reading off by a consistent **amount** | Offset drift (temperature, mechanical) | Re-TARE (IDLE only) |
| CALIBRATE "does nothing" | Session active → gated | Press STOP first, then calibrate |
| Jittery readings | Vibration / noise / too few samples | Stabilize platform; `LOADCELL_SAMPLES` = 3; tie HX711 RATE to 3.3 V for 80 SPS |
| Water loss wrong after a reboot mid-drying | TARE happened during the session (destroyed reference) | Shouldn't happen — TARE is gated; if it did, NVS was cleared/older firmware |

---

## 7. Calibration log

Record every calibration so drift is visible over time:

| Date | Firmware | TARE'd (Y/N) | Known mass (kg) | Saved factor | Verified ±2% |
|---|---|---|---|---|---|
| | | | | | |

---

## 8. Related

- Load cell driver: [`LOADCELL_CONFIG.h`](../../src/MultiDryerController/LOADCELL_CONFIG.h)
- Wiring: [`../schematic/hardware-wiring.md`](../schematic/hardware-wiring.md) §2.3
- Commands: [`../api/espnow-protocol.md`](../api/espnow-protocol.md) §3.1 (`0x40` TARE, `0x41` CALIBRATE)
- Bring-up: [`bring-up-checklist.md`](bring-up-checklist.md) Phase 1.4
