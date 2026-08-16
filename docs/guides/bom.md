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
| 3 | 3 | Solid-state relay | **SSR-40DA, 40 A — 3–32 VDC input / 220 VAC output** | SSR1 = heater, SSR3 = inlet fan, SSR4 = exhaust fan (wiring §1) |
| 4 | 1 | Heater | **PTC heater, 1500 W** | Switched by SSR1 (safety §6) |
| 5 | 2 | Fans | **220 VAC axial fans** | Inlet (SSR3) + exhaust (SSR4) |
| 6 | 4 | Load cells | **50 kg each, 4-wire** (Red→E+, Black→E−, Green→A+, White→A−) | Summed full-bridge on one HX711 → 200 kg total (wiring §2.3) |
| 7 | 1 | HX711 breakout | 24-bit ADC (10 SPS default) | Powered from 3V3 |
| 8 | 1 | Temp/humidity sensor | **SHT31 breakout, I2C addr 0x44** | 0x45 if ADDR tied HIGH; temp feedback for PID |
| 9 | 1 | Mains PSU | **220 VAC → 12 V DC, 5 A (60 W)** | Mains-isolated 12 V rail |
| 10 | 1 | Buck converter | **12 V → 5 V DC, 3 A** | Feeds ESP32 5V pin; onboard regulator makes 3.3 V |
| 11 | 3 | Fuses + holders | **10 A (heater) / 2 A (inlet fan) / 2 A (exhaust fan)** | One per AC branch, ~1.5× steady-state current |
| 12 | 1 | Thermal cutoff | Thermal fuse or bimetallic limit switch, rated just above max chamber temp | **Mandatory** — in series with heater branch only |
| 13 | 1 | Decoupling cap | 100 nF ceramic (HX711 VCC–GND) | Optional but recommended (wiring §2.3) |
| 14 | — | Heatsinks | For all 3 SSRs | + airflow past the SSR bank |
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
- **Separate switched exhaust outlet** — venting is done by the exhaust fan (SSR4)
  alone; there is no outlet SSR branch in this build (wiring §2.1).

---

## 3. Ordering Notes

- **Controller** — any 38-pin ESP32 DevKitC works, but confirm **WROOM-32 vs WROVER**
  (GPIO 16/17 differ). Buy 2 if you want a spare for bench experiments.
- **SSRs** — three identical **SSR-40DA 40 A** modules cover every branch (heater
  and both fans); the 40 A rating gives large headroom over the ~6.8 A heater load.
  Heatsinks come separately.
- **Load cells** — buy a **matched set of 4 × 50 kg** cells; wire all four in a
  summed full-bridge (all E+/E−/A+/A− paralleled) into the single HX711 channel →
  200 kg total capacity.
- **SHT31** — breakout modules usually include onboard I2C pull-ups; this build
  relies on the module's own pull-ups (none are added externally).
- **HMI** — the firmware board config is Waveshare-specific. If you use a different
  ESP32-S3 display, update `esp_panel_board_custom_conf.h` first.
- **Power** — 220 VAC → 12 V / 5 A PSU plus a 12 V → 5 V / 3 A buck converter.
  Check the buck's current rating: ESP32 + HX711 + SHT31 draw well under 3 A.
- **Local sourcing** (Philippines): Shopee / Lazada for DevKitC, HX711 + 4× 50 kg
  load-cell sets, SSR-40DA modules, and SHT31 breakouts; e-Gizmo / DIY Electronics
  (Manila) for 12 V PSUs, 12 V→5 V buck converters, fuses, heatsinks, and enclosure
  supplies. PTC heaters sized for food dryers are usually ordered online (1500 W
  chamber heater).
- **Fuses** — buy the rated cartridge/insert fuses plus holders; keep spares.

---

## 4. Related

- Wiring: [`../schematic/hardware-wiring.md`](../schematic/hardware-wiring.md) §2 & §6
- Safety & thermal: [`safety-and-thermal.md`](safety-and-thermal.md)
- Bring-up order: [`bring-up-checklist.md`](bring-up-checklist.md) — Phase 0 pre-flight
