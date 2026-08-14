# Test Plan — Multi Dryer

Verification strategy for the firmware: **host-side unit tests** for pure logic
(protocol + state machine), **command-handler tests** for the ESP-NOW edge cases,
and **HMI screen checks**. Hardware bring-up is covered separately by
[`bring-up-checklist.md`](bring-up-checklist.md) — this plan is the software layer
that runs *before and alongside* it.

> **Current state:** no test harness is committed yet — this plan is the spec for
> one. The state machine and protocol headers are written to be host-compilable
> with small stubs (same technique the headless screenshot renderer uses locally
> in `sim/`). Build the harness, fill the tables below, and run after every change
> to `espnow_protocol.h`, `DRYER_STATE.h`, or `espnow_link.h`.

---

## 1. Test Levels

| Level | What | Where | When |
|---|---|---|---|
| **Unit (host)** | Protocol structs/checksum, state machine transitions & math | Native build with stubs (`Serial`, `millis()`, `Preferences`, `scale`) | Every commit touching the shared header or state machine |
| **Command handler (host)** | `handleCmd` gating, ring buffer, arbitration | Same harness, fake `getDryerState()`/`startPID()` | Same |
| **HMI screen (visual)** | Every screen renders, navigation works, preset manager CRUD | Headless renderer (local `sim/`) + on-device | After any UI change; on-device at bring-up |
| **Hardware (bench)** | Boot, fail-safe, sensors, outputs, PID, thermal | Real rig | [`bring-up-checklist.md`](bring-up-checklist.md) |

---

## 2. Unit Tests — Protocol (`espnow_protocol.h`)

Both halves include this header — it must stay **byte-identical** (the copies drift
at your peril: struct sizes mismatch and every packet is rejected).

| ID | Test | Procedure | Expected |
|---|---|---|---|
| P1 | Struct sizes | `sizeof(EspNowStatusPacket)` / `sizeof(EspNowCmdPacket)` | **38** / **7** (packed, no padding) |
| P2 | Checksum vector | XOR a known byte array, compare to hand-computed value | Matches |
| P3 | Valid status packet | Fill all fields, set `checksum = espnowStatusChecksum()`, call `espnowStatusValid()` | `true` |
| P4 | Corrupted payload | Flip one byte mid-packet (not checksum), recompute nothing | `false` |
| P5 | Corrupted checksum | Flip only the checksum byte | `false` |
| P6 | Wrong type | Set `packetType` ≠ `ESPNOW_PKT_STATUS` with a valid checksum | `false` (type gate) |
| P7 | Cmd packet validity | Same as P3–P6 for `EspNowCmdPacket` / `espnowCmdValid()` | Per case |
| P8 | Header sync | `diff` the two `espnow_protocol.h` copies (controller vs HMI) | Identical |

---

## 3. Unit Tests — State Machine (`DRYER_STATE.h`)

Host harness with stubs: `millis()` returns a scripted clock, `readLoadCell()`
returns a scripted weight, `Preferences` backed by a RAM map, `Serial` a no-op
logger. Drive `updateDrying()` at 1 Hz by advancing the clock.

### 3.1 Transitions

| ID | Test | Procedure | Expected |
|---|---|---|---|
| S1 | IDLE → DRYING | `startDrying()` from IDLE | State `DRYING`, initial weight captured, PID started |
| S2 | Start while already DRYING | `startDrying()` twice | Second call is a no-op (stays DRYING) |
| S3 | DRYING → PAUSED | `pauseDrying()` | State `PAUSED`, PID stopped (all SSRs off) |
| S4 | PAUSED → DRYING | `resumeDrying()` | State `DRYING`, PID restarted |
| S5 | Resume when not PAUSED | `resumeDrying()` from IDLE | No-op |
| S6 | DRYING → COMPLETE | Drive weight to the water-loss target | Auto-completes: state `COMPLETE`, PID stopped |
| S7 | Any → IDLE | `stopDrying()` | State `IDLE`, session cleared, PID stopped |
| S8 | Restart from PAUSED | `startDrying()` while PAUSED | Fresh session (new initial weight, runtime reset) |

### 3.2 Water-loss math

| ID | Test | Procedure | Expected |
|---|---|---|---|
| W1 | Basic math | initW = 10.0 kg, weight = 8.0 kg | Water loss **20.0 %** |
| W2 | Negative clamp | weight > initW (gained weight / tare drift) | Clamped to **0.0 %** |
| W3 | Upper clamp | weight → 0 | Clamped to **100.0 %** |
| W4 | No usable init | initW ≤ 0.01 kg | Water loss stays **0.0 %** |
| W5 | Fresh-session reset | Stop → start with new weight | initW re-captured, loss resets to 0 |

### 3.3 Runtime & EDT

| ID | Test | Procedure | Expected |
|---|---|---|---|
| R1 | Accumulation | Advance 10 s in DRYING | `runtimeSeconds == 10` |
| R2 | Pause freezes | Pause, advance 30 s, resume | Runtime continues from 10 s (no pause drift) |
| R3 | EDT estimate | loss 10 % at 100 s, target 40 % | EDT ≈ (40−10)/(10/100) = **300 s** |
| R4 | EDT unknown rate | loss 0 % (rate < 0.0001 %/s) | EDT = **0** (unknown) |
| R5 | EDT after complete | Auto-complete reached | EDT = **0** |

### 3.4 NVS persistence & restore

| ID | Test | Procedure | Expected |
|---|---|---|---|
| N1 | No magic | Fresh namespace (magic absent) | No restore; state IDLE |
| N2 | Config-only restore | Saved marker IDLE + setpt/wlTgt | Setpoint & target restored, no session |
| N3 | Resume DRYING | Marker DRYING + initW/runMs/hxoff | State `DRYING`, runtime restored, HX711 offset re-applied, PID restarted |
| N4 | Resume PAUSED | Marker PAUSED | State `PAUSED`, PID **not** restarted |
| N5 | Config persist scope | `persistDryerConfig()` then inspect keys | Writes only `magic`/`wlTgt`/`setpt` — **never** `state`/`initW`/`runMs`/`hxoff` |
| N6 | Complete clears session | Auto-complete then inspect marker | `state` marker = IDLE (no resurrection on next boot) |
| N7 | Save cadence | Drying > 30 s, `_lastSaveMs` throttle | Session re-saved every ~30 s, not every tick |

---

## 4. Command-Handler Tests (`espnow_link.h` `handleCmd`)

Host harness with fake `getDryerState()` and spy `startPID()/stopPID()`/SSR flags.

| ID | Test | Procedure | Expected |
|---|---|---|---|
| C1 | TARE in IDLE | State IDLE, send `CMD_TARE_SCALE` | Accepted (scale tared) |
| C2 | TARE gated | State DRYING/PAUSED/COMPLETE, send TARE | **Ignored** (would destroy the zero reference) |
| C3 | CALIBRATE gated | Same gate as C2 | Accepted only in IDLE |
| C4 | Set temp | `CMD_SET_TEMPERATURE` with value | Setpoint updated **and** config persisted |
| C5 | Set water loss | `CMD_SET_WATER_LOSS` with value | Target updated, clamped 0–100, config persisted |
| C6 | Manual ends session | DRYING, send `CMD_HEATER_ON` | Session stopped (IDLE) **first**, then heater forced on |
| C7 | Manual in IDLE | IDLE, send `CMD_HEATER_ON`/`FAN_ON`/`EXHAUST_ON` | SSR flags reflect the override |
| C8 | Ring buffer order | Push 8 commands, drain | FIFO order, none lost |
| C9 | Ring buffer overflow | Push 10 commands, drain | First 8 kept (no corruption), 9th/10th dropped safely |

---

## 5. HMI Screen Checks

### 5.1 Automated (headless renderer — local `sim/`)

Rebuild the renderer and regenerate `docs/ui/*.png` after UI changes; visually
diff the outputs against the committed previews.

| ID | Screen | Check |
|---|---|---|
| H1 | Boot | "Multi-Purpose Dryer" title + subtitle render |
| H2 | Dashboard (idle & drying) | Live values, gauge needle, START/STOP/SET TEMP/HOW TO USE buttons |
| H3 | Control | Preset row from saved list, Custom, **+ Add** opens manager |
| H4 | Preset manager | Add (name + temp + water-loss), list with delete, keyboard works |
| H5 | Analytics | Temp/humidity/weight charts draw curves with data |
| H6 | Diagnostics | Status fields, TARE/CALIBRATE buttons |
| H7 | HOW TO USE | Opens from dashboard, 10 step cards, back arrow returns |

### 5.2 On-device (at bring-up, after `bring-up-checklist.md` §2.4 pairing)

- Every nav path: Dashboard ⇄ Control ⇄ Analytics ⇄ Diagnostics, Dashboard → HOW TO USE → back.
- Preset lifecycle: add → appears in row → tap applies temp + water-loss on the
  controller (verify via serial) → delete → gone after HMI reboot (NVS).
- Optimistic UI: button press gives instant feedback; the 1 Hz status packet
  corrects it within a second.
- Completion popup appears when the controller auto-completes.

---

## 6. Regression Rules

- **After any change** to `espnow_protocol.h` in either folder: run P1–P8 **and**
  the `diff` (P8) — the copies must stay identical.
- **After any change** to `DRYER_STATE.h` / `espnow_link.h`: run the full unit
  suite (S, W, R, N, C).
- **After any HMI screen change**: regenerate screenshots (H1–H7) and re-run the
  on-device nav check at next flash.
- Report results in the commit message or PR — the bring-up checklist §6.4 records
  hardware results.
