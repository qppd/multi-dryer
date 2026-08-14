# Multi Dryer — UI Screenshot Renderer

Compiles the **real HMI screens** from `src/MultiDryerHMI` against **LVGL 8.3**
on a desktop and renders each screen to a **PNG** in **`docs/ui/`** (at the
project root). No display, no SDL, no ESP32 needed — it draws into a memory
framebuffer and writes the result to disk.

What it renders:

| File (`docs/ui/`) | Screen |
|---|---|
| `boot.png` | Boot / splash ("Multi-Purpose Dryer") |
| `dashboard_idle.png` | Dashboard, IDLE |
| `dashboard_drying.png` | Dashboard, DRYING (live values) |
| `control.png` | Control — dynamic preset row (5 presets + Custom + Add) |
| `control_presets.png` | Control — preset manager modal open |
| `analytics_temperature.png` | Analytics — Temperature tab (filled chart) |
| `analytics_humidity.png` | Analytics — Humidity tab |
| `analytics_weight.png` | Analytics — Weight tab |
| `diagnostics.png` | System diagnostics |
| `how_to_use.png` | HOW TO USE guide |

## Requirements

- **CMake ≥ 3.16**
- A **C/C++ compiler** (Visual Studio, MinGW/MSYS2, GCC, or Clang)
- **git** (LVGL 8.3 is fetched from GitHub at configure time)
- Internet for the first configure (LVGL download)

## Build & run

Run the binary from this folder so the relative output path resolves:

```bash
cd sim/screenshot
cmake -S . -B build
cmake --build build
./build/ui_preview          # writes PNGs into ../../docs/ui/
```

Windows (MSVC):

```bat
cd sim\screenshot
cmake -S . -B build
cmake --build build --config Release
build\Release\ui_preview.exe
```

## How it works

- `src/main.cpp` — headless renderer: 800×480 RGB565 framebuffer, fake
  `dryerData` (idle + a drying session), a 2 s UI-update timer that mirrors
  `MultiDryerHMI.ino`, and PNG output via **lodepng** (bundled with LVGL —
  enabled with `LV_USE_PNG` in `lv_conf.h`).
- `src/stubs/` — host stand-ins for the ESP32-only pieces:
  - `Arduino.h` — minimal `millis`/`delay`/`String`/`Serial`/`ESP` shim
  - `Preferences.h` — in-memory NVS so the drying presets load/save
  - `lvgl_v8_port.h` — no-op lock helpers
  - `serial_protocol.cpp` — no-op command senders

The screen `.cpp` files themselves are the **unmodified production sources**.
