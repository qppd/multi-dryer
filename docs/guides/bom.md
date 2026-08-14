# Bill of Materials (BOM) — Multi Dryer

Parts list for ordering and assembly. Quantities are per **one** unit. Specs match
the wiring plan [`../schematic/hardware-wiring.md`](../schematic/hardware-wiring.md)
and the safety design [`safety-and-thermal.md`](safety-and-thermal.md).

> **Verify before ordering:** the controller board module type (WROOM vs WROVER,
> wiring plan §5.3), the HMI display board (must match
> `esp_panel_board_custom_conf.h` — currently **Waveshare ESP32-S3-Touch-LCD-7**),
> and the SSR input drive level (3–32 VDC input is required for direct 3.3 V drive).

---

## 1. Full BOM

| # | Qty | Item | Spec / Model | Notes |
|---|---|---|---|---|
| 1 | 1 | Controller board | **ESP32 38-pin DevKitC (WROOM-32)** | Verify module type — see §5.3 |
| 2 | 1 | HMI board | **ESP32-S3 + 7″ touch LCD (Waveshare ESP32-S3-Touch-LCD-7)** | 800×480 RGB + GT911; board config must match |
| 3 | 4 | Solid-state relay | **3–32 VDC input / 220 VAC output** (Fotek/Omron style) | 25 A for heater (SSR1), 10 A for fans (SSR3/SSR4) and outlet (SSR2) |
| 4 | 1 | Heater | **PTC heater, 500–2000 W** | Sized to chamber volume & target drying temp (safety §6) |
| 5 | 2 | Fans | **220 VAC axial fans** | Inlet + exhaust |
| 6 | 1 | Exhaust outlet | 220 VAC switched outlet/vent | Switched by SSR2 |
| 7 | 1 | Load cell | **1–50 kg, 4-wire** (Red→E+, Black→E−, Green→A+, White→A−) | |
| 8 | 1 | HX711 breakout | 24-bit ADC (10 SPS default) | Powered from 3V3 |
| 9 | 1 | Temp/humidity sensor | **SHT31 breakout, I2C addr 0x44** | 0x45 if ADDR tied HIGH; temp feedback for PID |
| 10 | 1 | Low-voltage PSU | **5 V isolated, ≥ 3 A** (e.g. Hi-Link HLK-PM03) | Feeds ESP32 5V pin; onboard regulator makes 3.3 V |
| 11 | 4 | Fuses + holders | **10 A (heater) / 5 A (outlet) / 2 A / 2 A (fans)** | One per AC branch, ~1.5× steady-state current |
| 12 | 1 | Thermal cutoff | Thermal fuse or bimetallic limit switch, rated just above max chamber temp | **Mandatory** — in series with heater branch only |
| 13 | 1 | Decoupling cap | 100 nF ceramic (HX711 VCC–GND) | Optional but recommended (wiring §2.3) |
| 14 | — | Heatsinks | For all 4 SSRs | + airflow past the SSR bank |
| 15 | — | Wire, terminals, glands, enclosure | Proper gauge AC wire, cable glands / separate ducting | Keep AC ≥ 6 mm from signal wiring |
| 16 | *opt* | NTC thermistor | On a spare ADC1 pin (GPIO 34/36/39) | Future firmware over-temp cutoff (safety §6) |

---

## 2. What's deliberately NOT on this BOM

- **Resistors / pull-downs** — the SSR fail-safe is firmware-enforced (pins forced
  LOW at the top of `setup()`); no external divider or pull-down network is required.
- **RC snubbers** — not required; add only if motor noise causes false SSR switching
  in your install (treat as a debug fix, not a build part).
- **5 V level shifter / transistor drivers** — HX711 runs at 3V3 with direct logic
  levels; SSRs must be 3–32 VDC input types driven straight from 3.3 V GPIOs.

---

## 3. Ordering Notes

- **Controller** — any 38-pin ESP32 DevKitC works, but confirm **WROOM-32 vs WROVER**
  (GPIO 16/17 differ). Buy 2 if you want a spare for bench experiments.
- **SSRs** — buy the **25 A** heater SSR and **10 A** fan SSRs as separate line items;
  don't assume one model covers all four. Heatsinks come separately.
- **SHT31** — breakout modules usually include onboard I2C pull-ups; this build
  relies on the module's own pull-ups (none are added externally).
- **HMI** — the firmware board config is Waveshare-specific. If you use a different
  ESP32-S3 display, update `esp_panel_board_custom_conf.h` first.
- **Local sourcing** (Philippines): Shopee / Lazada for DevKitC, HX711 + load cell,
  SSR-40DA/25DA modules, and SHT31 breakouts; e-Gizmo / DIY Electronics (Manila)
  for PSUs, fuses, heatsinks, and enclosure supplies. PTC heaters sized for food
  dryers are usually ordered online (500–2000 W chamber heaters).
- **Fuses** — buy the rated cartridge/insert fuses plus holders; keep spares.

---

## 4. Related

- Wiring: [`../schematic/hardware-wiring.md`](../schematic/hardware-wiring.md) §2 & §6
- Safety & thermal: [`safety-and-thermal.md`](safety-and-thermal.md)
- Bring-up order: [`bring-up-checklist.md`](bring-up-checklist.md) — Phase 0 pre-flight
