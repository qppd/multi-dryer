# Multi Dryer — Documentation

Central index of all project documentation.

## Existing Docs

| Doc | Path | Status |
|---|---|---|
| Wiring diagram (interactive) | [`schematic/wiring-diagram.md`](schematic/wiring-diagram.md) | ✅ link saved; **verify circuit against pins** |
| Hardware wiring plan (written) | [`schematic/hardware-wiring.md`](schematic/hardware-wiring.md) | ✅ complete |
| ESP-NOW protocol / API | [`api/espnow-protocol.md`](api/espnow-protocol.md) | ✅ complete |
| Technology stack | [`stacks/tech-stack.md`](stacks/tech-stack.md) | ✅ complete |
| Bring-up / boot checklist | [`guides/bring-up-checklist.md`](guides/bring-up-checklist.md) | ✅ complete |
| Calibration guide | [`guides/calibration-guide.md`](guides/calibration-guide.md) | ✅ complete |

> Written pin-by-pin plan, planning notes, and reference projects live in
> `references/` (local, git-ignored) — notably
> `references/plans/hardware-wiring.md` and `references/plans/multi-dryer-planning.md`.

## Required Docs — Roadmap

Docs that should exist before / during hardware bring-up, in priority order:

| # | Doc | Why it's required | Status |
|---|---|---|---|
| 1 | **Hardware wiring plan (written)** | Pin-by-pin reference for assembly & debugging | ✅ [`schematic/hardware-wiring.md`](schematic/hardware-wiring.md) |
| 2 | **Bring-up / boot checklist** | Ordered tests (boot, fail-safe, flash, WiFi, load cell, outputs, thermal) | ✅ [`guides/bring-up-checklist.md`](guides/bring-up-checklist.md) |
| 3 | **Calibration guide** | TARE + two-point `CALIBRATE:<kg>` procedure and how the factor/session offset persist in NVS | ✅ [`guides/calibration-guide.md`](guides/calibration-guide.md) |
| 4 | **Architecture overview** | Module diagram & data flow (sensors → PID → state machine → ESP-NOW → HMI) for onboarding | 📋 planned |
| 5 | **HMI user guide** | What each screen does, button semantics (start/pause/resume/stop, presets, manual mode) | 📋 planned |
| 6 | **Troubleshooting guide** | MAC pairing, no-status-packet, sensor-fail vent behavior, NVS session resurrection | 📋 planned |
| 7 | **Safety & thermal design** | SSR strategy, fuses, thermal cutoff, SHT31 vent guard, live-wiring rules | 📋 planned (content exists in wiring plan) |
| 8 | **BOM / procurement** | Parts list with specs & quantities for ordering | 📋 planned (draft in wiring plan §6) |
| 9 | **Test plan** | Unit/bench tests for the state machine & protocol helpers, HMI screen checks | 📋 planned |

## Conventions

- Keep protocol docs in sync with `espnow_protocol.h` (the header is the source of truth).
- Keep the shared header **byte-identical** in both firmware folders.
- Diagrams go in `docs/diagrams/`, schematics in `docs/schematic/`.
