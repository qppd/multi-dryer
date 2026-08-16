# Multi Dryer — Wiring Diagram

Interactive schematic (Cirkit Designer):

**https://app.cirkitdesigner.com/project/9d03db5a-37c8-4f46-ac3f-7d5821ce26ce**

> Requires a Cirkit Designer account/login to view.

> ⚠️ **Status:** the saved project still shows the **old 4-SSR design**
> (4 SSRs, 5 V PSU, single load cell, exhaust outlet, fuses 10/5/2/2 A).
> Apply the checklist below to bring it in line with the current BOM
> ([`guides/bom.md`](../guides/bom.md)) and wiring plan
> ([`hardware-wiring.md`](hardware-wiring.md)).

---

## Cirkit update checklist — 3-SSR build

Edit the project in the Cirkit Designer editor, then save + re-share the link.

### Parts (swap these)

| # | In the project now | Change to | Qty |
|---|---|---|---|
| 1 | SSR modules ×4 (mixed 25/10 A) | **SSR-40DA, 40 A — 3–32 VDC input / 220 VAC output** | **3** |
| 2 | PTC heater 500–2000 W | **PTC heater, 1500 W** | 1 |
| 3 | Load cell ×1 (1–50 kg) | **Load cells ×4 (50 kg each)**, wired as a summed full-bridge (all E+/E−/A+/A− paralleled into the one HX711 channel) | **4** |
| 4 | 5 V isolated PSU | **220 VAC → 12 V / 5 A PSU** + **12 V → 5 V / 3 A buck converter** | 1 + 1 |
| 5 | Fuses 10 A / 5 A / 2 A / 2 A | **10 A (heater) / 2 A (inlet fan) / 2 A (exhaust fan)** — delete the 5 A outlet fuse | **3** |
| 6 | Exhaust outlet + its SSR | **Remove entirely** — venting is the exhaust fan (SSR4) | 0 |

### Connections (re-wire these)

1. **AC side** — remove the SSR2 branch (`F2 5A fuse → SSR2 → exhaust outlet`).
   Remaining branches, all returning to AC-N:
   ```
   AC-L ──┬─[F1 10A]──[Thermal cutoff]── SSR1 ── PTC Heater ──┐
          ├─[F2  2A]── SSR3 ── Inlet Fan ─────────────────────┤── AC-N
          └─[F3  2A]── SSR4 ── Exhaust Fan ───────────────────┘
   ```
2. **SSR inputs (3.3 V logic)** — SSR(+) → GPIO, SSR(−) → GND:
   - SSR1 → **GPIO 26** (heater)
   - SSR3 → **GPIO 27** (inlet fan)
   - SSR4 → **GPIO 13** (exhaust fan)
   - **GPIO 25 no longer wired to anything** (old SSR2 pin, now spare)
3. **HX711 + load cells** — four cells paralleled into one HX711:
   - Each cell: Red→E+, Black→E−, Green→A+, White→A−, Shield→GND (all E+/E−/A+/A− tied together)
   - HX711: DOUT→**GPIO 35**, SCK→**GPIO 32**, VCC→**3V3**, GND→GND
4. **SHT31** — SDA→**GPIO 21**, SCL→**GPIO 22**, VIN→3V3, GND→GND (addr 0x44)
5. **Power** — 220 VAC → PSU → **12 V rail** → buck converter input; buck output
   **5 V** → ESP32 **5V** pin (onboard regulator makes 3.3 V). Common ground:
   12 V PSU GND → buck GND → ESP32 GND → HX711 GND.

### Verify after editing

- Only 3 SSR devices in the parts list; no exhaust-outlet branch or 5 A fuse.
- GPIO 26/27/13 drive the three SSRs; GPIO 25 unconnected.
- Heater branch: `F1 10A → thermal cutoff → SSR1 → PTC heater (1500 W)`.
- Low-voltage side: PSU → 12 V → buck → 5 V → ESP32.

---

## Related

- Written pin-by-pin wiring plan: [`hardware-wiring.md`](hardware-wiring.md)
- BOM / procurement: [`../guides/bom.md`](../guides/bom.md)
- Pin definitions: `src/MultiDryerController/PINS_CONFIG.h`
