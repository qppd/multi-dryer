# Safety & Thermal Design

The Multi Dryer switches 220 VAC loads in an enclosure that also holds an
ESP32, sensors, and a food-drying chamber. Safety is designed in **layers** —
no single failure may lead to an unsafe state (stuck-on heater, thermal
runaway, or mains exposure).

> Related: [`bring-up-checklist.md`](bring-up-checklist.md) Phase 0/4 ·
> [`troubleshooting.md`](troubleshooting.md) §2 ·
> [`../schematic/hardware-wiring.md`](../schematic/hardware-wiring.md) §2

---

## 1. Defense-in-Depth Layers

| Layer | Mechanism | Protects against |
|---|---|---|
| 1. Mechanical | Thermal cutoff (fuse/limit switch) in series with the heater branch | Stuck-on SSR, fan failure, firmware crash |
| 2. Electrical | Fuse per AC branch; opto-isolated SSRs | Overcurrent, short circuit, mains-to-logic faults |
| 3. Boot-time | Firmware forces every SSR pin LOW as the first step of `setup()` | SSR energizing during the boot window |
| 4. Firmware | **SHT31 vent guard**, PID output limits, TARE gate, manual/auto arbitration | Dead sensor → full-power heating; conflicting control |
| 5. User | Enclosure isolation, wiring separation, clear labeling | Electric shock, fire |

---

## 2. AC Power Distribution

```
AC-L ──┬─[F1 10A]──[Thermal cutoff]── SSR1 (load) ── PTC Heater ──┐
       ├─[F2  2A]── SSR3 (load) ── Inlet Fan ────────────────────┤── AC-N
       └─[F3  2A]── SSR4 (load) ── Exhaust Fan ──────────────────┘
```

- **Fuses:** 10 A on the heater branch (1500 W / 220 V ≈ 6.8 A → ~1.5×), 2 A on
  each fan branch. Sized ~1.5× steady-state current. There is **no separate
  outlet branch** — the exhaust fan (SSR4) provides venting.
- **Thermal cutoff (mandatory):** rated just above the maximum expected chamber
  temperature, wired **in series with the PTC heater** only. If the SSR sticks
  on or a fan dies, the cutoff opens the heater circuit mechanically.
- **Neutral/common:** all branches return to AC-N; no switching on the neutral.

---

## 3. SSR Strategy

| Item | Requirement |
|---|---|
| SSR input | 3–32 VDC (Fotek/Omron style) → driven directly by 3.3 V logic (5–10 mA) |
| SSR output | 220 VAC, **derated** — 3 × SSR-40DA **40 A** modules (heater 1500 W ≈ 6.8 A, fans ≈ 1 A; large headroom, minimal heat) |
| Isolation | Opto-isolated AC side — the mains never touches the ESP32 electrically |
| Fail-safe | Firmware forces every SSR input LOW at the top of `setup()` — outputs stay OFF until explicitly enabled |
| Thermal | Heat sinks on all SSRs; AC wiring separated from signal wiring |

Drive circuit per output:

```
ESP32 GPIO ── SSR input (+)      SSR input (−) ── GND
```

> **3.3 V-logic SSR inputs are required** — the ESP32 drives SSR(+) directly
> (3–32 VDC input types). If a module needs a higher drive level, use a
> 3.3 V-logic-compatible SSR rather than adding external driver circuitry.

---

## 4. Boot-Time Safety

1. Firmware sets every SSR pin `pinMode(OUTPUT); digitalWrite(LOW);` **as the first thing in `setup()`**, before WiFi/ESP-NOW init — no SSR can energize until the firmware explicitly enables it. (The 3-SSR build drives SSR1, SSR3, and SSR4 only.)
2. Pin selection avoids boot hazards: no strapping pins (0/2/4/5/12/15), no GPIO 14 (PWM burst at boot), no flash/UART0 pins — so an SSR can never be driven HIGH by default reset behavior.

---

## 5. Firmware Safety

### 5.1 SHT31 vent guard (the critical rule)
In `PID_CONFIG.h::pidCOMPUTE()` — if the temperature feedback is lost
(`!sht31OK`) while the PID is in AUTOMATIC:

```
PID_OUTPUT = 0
SSR1 (heater)  = OFF      SSR3 (inlet fan)  = OFF
SSR4 (exhaust fan) = ON                          ← vent instead of heat
```

**A dead sensor can never cause full-power heating.** The unit vents, `FLAG_SHT31`
clears on the HMI, and the status packet reflects the real SSR state.

### 5.2 Output limits & arbitration
- PID output clamped 0–5000 (`SetOutputLimits`) — the SSR drive is binary
  (heat vs. vent), so this bounds the duty model.
- Any **manual** HEATER/FAN/EXHAUST command **ends an active session first**
  (`espnow_link.h::handleCmd`) — manual and automatic control can never fight.

### 5.3 Calibration gate
TARE / CALIBRATE are ignored unless the state machine is **IDLE**, so a
boot-time TARE can't destroy a restored session's weight reference.

### 5.4 Restored sessions
On boot-resume, the PID restarts only for DRYING sessions and only if the SHT31
guard passes — a session resumed with a dead sensor vents, it doesn't heat.

---

## 6. Thermal Design

| Aspect | Guidance |
|---|---|
| Heater sizing | 1500 W PTC, matched to chamber volume and target drying temp |
| Airflow | Inlet fan runs whenever the heater is on (SSR1+SSR3 together) — no heater without airflow |
| Exhaust | Exhaust fan (SSR4) on when venting — removes humid air and excess heat |
| Chamber sensor | SHT31 near the product (not directly in heater blast) for accurate feedback |
| Over-temp option | Optional NTC thermistor on a spare ADC1 pin (GPIO 34/36/39) for a firmware high-limit cutoff — listed as a future enhancement |
| SSR cooling | Heat sinks + airflow past the SSR bank; keep AC wiring ≥ 6 mm from signal lines |

**Thermal cutoff test:** heat the limit switch to its rating → the heater branch
must clear. See `bring-up-checklist.md` §4.5.

---

## 7. Enclosure & User Safety

- 220 VAC → 12 V 5 A PSU feeding a 12 V → 5 V 3 A buck converter; the 5 V rail
  feeds the ESP32's 5V pin (onboard regulator makes 3.3 V).
- One common ground: 12 V PSU GND → buck GND → ESP32 GND → HX711 GND (tie HMI ground too, though it's wireless).
- Never route 220 V wiring near signal pins; use cable glands / separate ducting.
- Fuses accessible; thermal cutoff replaceable; clear labels on the enclosure.
- Live-wiring rule: **always unplug mains before opening or servicing.**

---

## 8. Failure-Mode Analysis

| Failure | Result without safeguards | Active safeguard | Result with safeguards |
|---|---|---|---|
| SSR1 sticks ON | Heater runs uncontrolled → fire | Thermal cutoff opens heater branch | Heater circuit physically cleared |
| Inlet fan fails (heater on) | Overheat, no airflow | Thermal cutoff + PID sees rising temp → output 0 → vent | Venting, heater off |
| SHT31 dies mid-cycle | PID heats blind at full power | Vent guard (heater OFF, vent ON) | Safe, vents; HMI shows SHT31 flag clear |
| Controller crashes/resets | SSRs hold last state (bad) | Firmware forces pins LOW at boot + re-inits MANUAL | All outputs OFF at reset |
| WiFi/ESP-NOW loss | Control link lost | Controller runs autonomously (state machine keeps safety rules) | Drying continues safely; HMI stale |
| Command burst floods controller | Dropped/lost commands | 8-slot ring buffer | No loss |
| Brown-out | Random SSR state | 5 V buck ≥ 3 A + bulk capacitance; watchdog resets to safe boot | OFF at boot |

---

## 9. Verification

- **Bring-up:** `guides/bring-up-checklist.md` — Phase 0 (pre-flight), Phase 1.2
  (fail-safe), Phase 4 (PID & thermal, incl. §4.4 vent-guard test and §4.5
  cutoff test), Phase 6 (soak).
- **Golden rule on the bench:** a dead SHT31 must result in **venting, never
  heating** — test this explicitly every bring-up (checklist §4.4).
