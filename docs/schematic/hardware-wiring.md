# Multi Dryer — Hardware Wiring Plan (v1)

Target board: **ESP32 38-pin DevKitC** (ESP32-WROOM-32 module, no PSRAM — verify module type, see §5.3).
HMI board: **ESP32-S3 + touch** (separate board, communicates wirelessly via **ESP-NOW** — same transport the HMIDisplay reference already uses).

Design goal: every pin on the controller is **boot-safe** (no strapping/flash/boot-PWM pins) and **WiFi-safe** (no analog use on ADC2 pins, which become unusable when WiFi/ESP-NOW is active).

---

## 1. Pin Allocation (final)

| Function | GPIO | Board label | Notes |
|---|---|---|---|
| **SSR1 — PTC heater** (power stage) | **26** | D26 | clean digital output |
| **SSR3 — inlet air fan** | **27** | D27 | clean digital output |
| **SSR4 — exhaust fan** | **13** | D13 | clean digital output |
| **HX711 — DOUT** (load cell data) | **35** | D35 | input-only pin — perfect (read-only) |
| **HX711 — SCK** (load cell clock) | **32** | D32 | clean digital output |
| **SHT31 — temp/humidity (I2C)** | **21 (SDA), 22 (SCL)** | | hardware I2C; temp feedback for PID (FishDryer baseline); driver: `SHT31_CONFIG.h` |
| Spare digital | 16, 17, 18, 19, 23, 25, 33 | | 16/17 = WROOM-32 only (PSRAM on WROVER); 18/19/23 = default VSPI; 25 = freed ex-SSR2 pin |
| Spare input-only | 34, 36, 39 | | future analog sensors (ADC1, WiFi-safe) |
| **Avoid — strapping pins** | 0, 2, 4, 5, 12, 15 | | see §3.1 |
| **Avoid — flash pins** | 6, 7, 8, 9, 10, 11 | | connected to SPI flash inside module |
| **Avoid — UART0** | 1 (TX), 3 (RX) | | USB-serial for flashing/debug only |

> Only **3 SSRs** in this build: SSR1 (heater), SSR3 (inlet fan), SSR4 (exhaust
> fan). The exhaust outlet was dropped — venting is done by the exhaust fan
> (SSR4), and GPIO 25 (the old SSR2 pin) is free as spare digital.
>
> **Do not connect anything to GPIO 14** for outputs: it emits a PWM burst at boot (would glitch an SSR). GPIO 1/3 also drive HIGH at boot — keep them free for the serial monitor.

---

## 2. Wiring Diagrams

### 2.1 AC Power Side (220 V)

```
AC-L ──┬─[F1 10A fuse]───[Thermal cutoff]── SSR1 (load term.) ── PTC Heater ──┐
       ├─[F2  2A fuse]─── SSR3 (load term.) ── Inlet Fan ──────┤── AC-N
       └─[F3  2A fuse]─── SSR4 (load term.) ── Exhaust Fan ────┘
```

- **Fuse sizing:** 10 A on the heater branch (1500 W / 220 V ≈ 6.8 A → ~1.5×), 2 A
  on each fan branch. There is **no separate outlet branch** — the exhaust fan
  (SSR4) provides venting.
- **Thermal cutoff (mandatory):** a thermal fuse or bimetallic limit switch wired **in series with the PTC heater** branch, rated just above max chamber temperature. Protects against a stuck-on SSR or fan failure. Optionally add an NTC thermistor on a spare ADC1 pin (GPIO 34/36/39 — WiFi-safe) for a firmware over-temperature cutoff.

- Each SSR's **AC side is opto-isolated** from its DC input — the 220 V side never touches the ESP32 electrically.
- Use SSRs rated for **3–32 VDC input** (Fotek/Omron style) so the ESP32's 3.3 V logic drives them directly (≈5–10 mA).
- Mount SSRs on heat sinks; keep AC wiring physically separated from all low-voltage signal wiring (cable glands / separate ducting).

### 2.2 SSR Drive Circuit (per output × 3)

```
ESP32 GPIO ── SSR input (+)      SSR input (−) ── GND
```

- **Fail-safe is firmware-enforced:** every SSR pin is set `pinMode(OUTPUT); digitalWrite(LOW);` as the **first thing in `setup()`** (before WiFi/ESP-NOW init), so no SSR can energize until the firmware explicitly enables it.
- Verify the SSR's input triggers reliably at 3.3 V logic (3–32 VDC input types). If the module needs a higher drive level, use a 3.3 V-logic-compatible SSR rather than adding external driver circuitry.

### 2.3 Load Cell + HX711

```
Load cells (4 × 50 kg, summed full-bridge): wire each cell the same way and
parallel all four — Red→E+   Black→E−   Green→A+   White→A−   Shield→GND
(all E+ together, all E− together, all A+ together, all A− together)

HX711 module          ESP32 (38-pin)
  VCC      ──────────  3V3
  GND      ──────────  GND
  DOUT     ──────────  GPIO 35      (input-only pin — safe)
  SCK      ──────────  GPIO 32
  RATE/GND ──────────  leave default (10 SPS)
```

- The four 50 kg cells sum into **one bridge** on the single HX711 channel →
  200 kg total capacity. With typical 350 Ω cells the paralleled bridge loads the
  3V3 rail with only ~87 Ω (≈38 mA) — fine for the onboard regulator.
- Keep the load cell cables short, twisted, and away from the AC/SSR wiring (noise immunity). Add a 100 nF decoupling cap across HX711 VCC–GND.

### 2.4 Controller ⇄ HMI (ESP-NOW, wireless)

```
Controller (ESP32 38-pin)          HMI (ESP32-S3 + touch)
        ┌─────────────────┐                ┌─────────────┐
        │ WiFi STA + ESP-NOW ════════════► │ WiFi STA    │
        └─────────────────┘                └─────────────┘
               GND (both boards) ── common ground recommended
```

- **No signal wires between the boards.** Protocol header `src/MultiDryerController/espnow_protocol.h` is byte-identical to the HMIDisplay reference, so the two sides interoperate as-is.
- Controller link driver: `src/MultiDryerController/espnow_link.h` — sends **EspNowStatusPacket** at 1 Hz carrying state, SHT31 temp/humidity, load cell weight (kg), water loss, setpoint, PID output, runtime and EDT; receives **EspNowCmdPacket** commands — the full protocol set (drying control, PID, calibration, manual overrides) is implemented.
- **MAC setup (one-time):**
  1. Boot controller → serial prints `Controller MAC: XX:…`
  2. Paste into `CONTROLLER_PEER_MAC` in the HMI's `src/MultiDryerHMI/serial_protocol.cpp`
  3. Boot HMI → serial prints `Controller peer MAC: YY:…`
  4. Paste into `HMI_PEER_MAC` in the controller's `espnow_link.h`, re-flash controller
- Set both to the same channel (`ESPNOW_WIFI_CHANNEL`, default 1).
- **ESP-NOW is the ONLY board-to-board transport** — no UART and no I2C between controller and HMI (decision 2026-08-07). GPIO 16/17 stay free, and the SHT31 owns the I2C bus.
- The serial (UART0) ports of both boards are used **only** for local flashing/debug, never for inter-board comms.

### 2.5 SHT31 Temperature/Humidity (part of the build)

```
SHT31  SDA ── GPIO 21      SHT31  SCL ── GPIO 22      VIN ── 3V3      GND ── GND
```

- Address **0x44** (0x45 if ADDR tied HIGH). Driver: `src/MultiDryerController/SHT31_CONFIG.h` (non-blocking state machine, CRC-8, ported from the FishDryer baseline onto the hardware I2C bus).
- Temp feedback feeds PTC control/PID, and temp/humidity are reported to the HMI over ESP-NOW.

---

## 3. Boot Compatibility — Why This Allocation Is Safe

> Note: GPIO 21/22 (SHT31 I2C) are clean digital pins with no boot behavior — they are safe as an I2C bus.


### 3.1 Strapping pins avoided

| GPIO | Strapping behavior | Consequence if used carelessly |
|---|---|---|
| 0 | LOW at reset → download mode | device won't boot from flash |
| 2 | must be floating/LOW at reset | pull-ups (e.g., SSR input) can break flashing |
| 4 | JTAG source select | boot behavior changes with external pull |
| 5 | must be HIGH at reset | a pulldown on it can trap the board in SD boot |
| 12 | boot fails if HIGH at reset | external pull-ups can brick boot |
| 15 | must be HIGH at reset | pulldowns can hang boot |

None of 0/2/4/5/12/15 appear in the allocation.

### 3.2 Flash pins avoided

GPIO 6–11 are bonded to the module's SPI flash — never available. Not used.

### 3.3 No boot-PWM/HIGH pins used for outputs

GPIO 1, 3 (serial), 5, 14, 15 drive HIGH or PWM at reset. An SSR on GPIO 14 could click/pulse briefly at every power-on; we avoid that class entirely. All SSR lines are plain, clean GPIOs, and the firmware forces them LOW at the top of `setup()`, so outputs stay OFF until explicitly enabled.

### 3.4 UART0 untouched

GPIO 1/3 remain free → flashing and serial debugging always work with peripherals connected.

### 3.5 Verify with the boot checklist (§7)

---

## 4. WiFi / ESP-NOW Compatibility

1. **No analog reads on ADC2 pins.** With WiFi (or ESP-NOW) active, ADC2 (GPIO 4, 0, 2, 15, 13, 12, 14, 27, 25, 26) returns invalid values. The plan deliberately uses ADC2-capable pins (13/26/27; 25 stays spare) as **digital outputs only**, and the load cell path is fully digital (HX711). Nothing depends on ADC2 analog.
2. **Future analog sensors** must use ADC1 (GPIO 32–39): GPIO 34, 36, 39 stay reserved for that.
3. **ESP-NOW needs WiFi STA mode** (`WiFi.mode(WIFI_STA)`), which the reference HMI already does. No conflicts with the chosen pins.
4. If the controller ever connects to a real network (OTA, telemetry), same rules apply — no changes needed to the pin map.

---

## 5. Power & Isolation Notes

### 5.1 Low-voltage supply
- **220 VAC → 12 V / 5 A** mains PSU → 12 V rail.
- **12 V → 5 V / 3 A** buck converter → ESP32 **5V** pin. The DevKitC's onboard regulator makes 3.3 V.
- Optional 5 V for HX711: see below.

### 5.2 HX711 supply
- **HX711 VCC → 3V3** (recommended): direct logic levels, no level shifting. Slightly lower bridge excitation — fine for the 4× 50 kg paralleled bridge.
- **DOUT floats when the HX711 is unpowered** (GPIO 35 is input-only with no internal pull-up) — firmware must gate reads on `scale.is_ready()` (the FishDryer baseline already does) so a floating DOUT is never mistaken for a valid reading.

### 5.3 Verify module type (important!)
- 38-pin DevKitC boards ship with **WROOM-32** (GPIO 16/17 free) or **WROVER** (GPIO 16/17 used by PSRAM — do not touch them). The allocation above lists 16/17 as spare; nothing critical depends on them.

### 5.4 General safety
- One common ground: 12 V PSU GND → buck GND → ESP32 GND → HX711 GND. If the HMI is on a separate PSU, tie the two boards' grounds (ESP-NOW is wireless, but shared reference is safer for any shared lines).
- Fuses on every AC branch (§2.1), correct wire gauge, heatsinks on SSRs.
- Never route 220 V wiring near signal pins; keep ≥ 6 mm separation on any custom PCB.

---

## 6. Suggested BOM

| Item | Spec | Qty |
|---|---|---|
| ESP32 38-pin DevKitC (WROOM-32) | 38-pin, WROOM (verify) | 1 |
| SSR-40DA (3–32 VDC input / 220 VAC output) | Fotek-style 40 A, one per branch | 3 |
| PTC heater | 1500 W | 1 |
| Inlet + exhaust fans | 220 VAC axial fans | 2 |
| HX711 breakout | 24-bit ADC | 1 |
| Load cells | 4 × 50 kg, 4-wire (summed bridge) | 4 |
| SHT31 breakout | I2C temp/humidity, addr 0x44 | 1 |
| 220 VAC → 12 V PSU | 5 A (60 W) | 1 |
| 12 V → 5 V buck converter | 3 A | 1 |
| Fuses | 10 A / 2 A / 2 A | 3 |
| Wire, terminals, heatsinks, enclosure | — | — |

---

## 7. Verification Checklist

1. **Boot test:** with everything connected, open the serial monitor — board must boot normally (no download-mode loop, no crash). 
2. **Fail-safe test:** with everything connected, power on and confirm **no SSR/load toggles** during the boot window — the firmware forces every SSR pin LOW at the top of `setup()`, before WiFi/ESP-NOW init.
3. **Flash test:** `esptool.py chip-id` / Arduino upload must succeed with peripherals attached (proves no strapping pin is loaded down).
4. **WiFi/ESP-NOW test:** enable ESP-NOW (or run `WiFi.scanNetworks()`) with SSRs + HX711 connected — no hangs, and control still works (proves ADC2/WiFi rule is not violated).
5. **Load cell test:** send `TARE` then `CALIBRATE:<kg>` (FishDryer protocol) — weight reads correctly on the HMI.
6. **Output test:** toggle SSR1/SSR3/SSR4 (populated SSRs) from the HMI one at a time; verify with a multimeter/clamp meter on the AC side.
7. **Thermal test:** confirm the thermal cutoff clears the heater branch at its rated temperature (simulate by heating the limit switch).
8. **setup() order test:** confirm SSR pins are forced LOW at the top of `setup()`, before WiFi/ESP-NOW init — no SSR toggles during boot.
