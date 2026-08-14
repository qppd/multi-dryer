# Bring-Up / Boot Checklist

Ordered hardware bring-up procedure for the Multi Dryer. Go top to bottom —
each phase depends on the previous one. Tick boxes as you pass each check.

> References: wiring plan [`../schematic/hardware-wiring.md`](../schematic/hardware-wiring.md),
> protocol [`../api/espnow-protocol.md`](../api/espnow-protocol.md),
> stack [`../stacks/tech-stack.md`](../stacks/tech-stack.md).

---

## Phase 0 — Pre-Flight (bench, unpowered)

- [ ] **0.1** Visual check: no solder bridges, shorts, or cold joints on SSR inputs, HX711, SHT31.
- [ ] **0.2** Continuity: each GPIO → SSR input is wired directly, with no short to GND or the AC side.
- [ ] **0.3** Power rails separated: 220 VAC side never adjacent to signal wires (< 6 mm rule).
- [ ] **0.4** Fuses fitted on every AC branch (10 A heater / 5 A outlet / 2 A fans).
- [ ] **0.5** Thermal cutoff wired **in series with the PTC heater** branch.
- [ ] **0.6** Load cell wired correctly: Red→E+, Black→E−, Green→A+, White→A−, Shield→GND.
- [ ] **0.7** SHT31 on the I2C bus: SDA→21, SCL→22, VIN→3V3, GND→GND, addr 0x44 (0x45 if ADDR tied HIGH).
- [ ] **0.8** Bench meter / clamp meter ready for AC-side output checks.

---

## Phase 1 — Controller Only (no AC loads, no HMI)

**Goal:** prove the board boots, the pins are safe, and the sensors read.

### 1.1 Boot test
- [ ] **1.1a** Flash the controller sketch (`src/MultiDryerController`).
- [ ] **1.1b** Open the serial monitor (115200) — board boots normally: **no download-mode loop, no crash/reboot**.
- [ ] **1.1c** Boot banner appears in order: pins → SHT31 → load cell → PID → drying → ESP-NOW.

### 1.2 Fail-safe test
- [ ] **1.2a** After boot, measure each SSR input — must read **LOW** (firmware forces pins LOW at the top of `setup()`).
- [ ] **1.2b** Heater and fans stay off during the ~1 s boot window (no SSR clicks).
- [ ] **1.2c** Firmware forces SSR pins LOW first thing in `setup()` — verify no HIGH glitch on GPIO 26/25/27/13.

### 1.3 Flash test (with peripherals attached)
- [ ] **1.3a** `esptool.py chip-id` / Arduino upload **succeeds** with SSRs + HX711 + SHT31 all connected (proves no strapping pin is loaded down).
- [ ] **1.3b** Repeat upload 3× — no download-mode traps.

### 1.4 Sensor tests
- [ ] **1.4a** SHT31: serial shows plausible temp (~room temp) and humidity; `[SHT31] OK` (or the state-machine status line shows `temp=…`).
- [ ] **1.4b** SHT31 CRC: no CRC failures in a 2-minute window.
- [ ] **1.4c** Load cell: `TARE` via the HMI or serial command → weight ≈ 0.000 kg.
- [ ] **1.4d** Load cell: `CALIBRATE:<kg>` with a known weight → reading tracks within ±2%.

---

## Phase 2 — WiFi / ESP-NOW

- [ ] **2.1** Controller serial prints `[ESP-NOW] Controller MAC: XX:…` and `Link ready`.
- [ ] **2.2** Run `WiFi.scanNetworks()` (or just leave ESP-NOW active) with all peripherals connected — **no hangs**, control still responsive (proves the ADC2/WiFi rule isn't violated).
- [ ] **2.3** Note the controller MAC.

### 2.4 HMI pairing
- [ ] **2.4a** Flash the HMI sketch (`src/MultiDryerHMI`) on the ESP32-S3 — display initializes (splash → boot screen).
- [ ] **2.4b** HMI serial prints its own MAC.
- [ ] **2.4c** Paste controller MAC into `CONTROLLER_PEER_MAC` (HMI `serial_protocol.cpp`), re-flash HMI.
- [ ] **2.4d** Paste HMI MAC into `HMI_PEER_MAC` (controller `espnow_link.h`), re-flash controller.
- [ ] **2.4e** Both serial monitors show clean sends; dashboard shows **live values** (temp/humidity/weight) updating ~1 Hz.
- [ ] **2.4f** No `Add peer FAILED` / `Send failed — check …_PEER_MAC` messages.

---

## Phase 3 — Outputs (one at a time, AC connected)

> Verify each with a multimeter/clamp meter on the AC side. Watch current draw.

- [ ] **3.1** `HEATER_ON` → SSR1 energizes (PTC) **and** SSR3 (inlet fan) — airflow with heat.
- [ ] **3.2** `HEATER_OFF` → all outputs off.
- [ ] **3.3** `FAN_ON` → SSR3 (inlet fan) only; `FAN_OFF` → off.
- [ ] **3.4** `EXHAUST_ON` → SSR2 (outlet open) + SSR4 (exhaust fan); `EXHAUST_OFF` → off.
- [ ] **3.5** Dashboard indicators (Heater/Fan/Exhaust) match the real SSR states within 1 s.

---

## Phase 4 — PID & Thermal

- [ ] **4.1** Set a setpoint (e.g. 60 °C) on the HMI → heater cycles; temperature rises toward the setpoint.
- [ ] **4.2** Temperature settles near the setpoint without excessive overshoot (tune KP/KD if not).
- [ ] **4.3** Above target: PID output → 0, unit vents (SSR2+SSR4 on, heater off).
- [ ] **4.4** **Safety guard:** unplug/disable the SHT31 mid-cycle → heater forced OFF, unit vents, HMI flags SHT31 absent (`FLAG_SHT31` clear).
- [ ] **4.5** **Thermal cutoff:** heat the limit switch to its rating → heater branch clears (circuit trips) — no heating until reset.

---

## Phase 5 — State Machine & Persistence

- [ ] **5.1** `START_DRYING` → state DRYING, runtime counts, water loss decreases as weight drops.
- [ ] **5.2** `PAUSE_DRYING` → all SSRs off, state PAUSED; runtime **freezes**.
- [ ] **5.3** `RESUME_DRYING` → PID back on, runtime continues.
- [ ] **5.4** `STOP_DRYING` → state IDLE, session cleared.
- [ ] **5.5** **Auto-complete:** set a water-loss target, let weight reach it → state COMPLETE, outputs off, HMI shows the completion dialog.
- [ ] **5.6** **Reboot-resume:** start a session, power-cycle the controller → on boot it resumes (DRYING with PID, or stays PAUSED); weight reference intact (water loss not corrupted).
- [ ] **5.7** **Config persistence:** set temperature + water-loss target, reboot → values restored even with no session active.
- [ ] **5.8** **TARE gate:** while a session is active, a boot-time TARE is **ignored** (weight/water loss unchanged).

---

## Phase 6 — Final Integration Sign-Off

- [ ] **6.1** Full cycle from the HMI: configure → START → PAUSE → RESUME → auto-COMPLETE → STOP.
- [ ] **6.2** 30-minute soak: no resets, no sensor lockups, temperature stable, water loss tracking.
- [ ] **6.3** Log a full run in the diagnostics screen (analytics fields populated).
- [ ] **6.4** Record results: date, firmware hashes, tuned KP/KD values, calibration factor → update this checklist.

---

## Troubleshooting Quick Reference

| Symptom | Likely cause | Fix |
|---|---|---|
| Board stuck in download mode / won't boot | A strapping pin loaded down (0/2/4/5/12/15) | Check wiring on strapping pins; remove pull-ups |
| SSR clicks at power-on | GPIO 14 used as an output (PWM burst at boot) | Move to a clean GPIO; never use GPIO 14 |
| No SHT31 data | Wrong pins, wrong addr | Check SDA/SCL, try address 0x45 |
| Weight reads garbage / floats | HX711 offset drift or floating DOUT | `TARE`; ensure HX711 powered; check DOUT/SCK |
| No status packets on HMI | Peer MACs not set / channel mismatch | Complete §2.4; both on channel 1 |
| Heater never on | Setpoint 0 / SHT31 lost (vent guard) | Send `SET_TEMPERATURE`; check sensor |
| Session resurrects as DRYING after completion | Old NVS session marker | Press STOP after COMPLETE; clear NVS if needed |
