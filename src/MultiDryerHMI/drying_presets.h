// drying_presets.h
// Multi Dryer HMI — named drying presets (product profiles) stored in NVS.
//
// A preset bundles the three values a product needs to be dried:
//   - name      (product type, e.g. "Anchovy")
//   - tempC     (drying temperature setpoint, sent as SET_TEMPERATURE)
//   - wlTarget  (water-loss auto-complete target %, sent as SET_WATER_LOSS)
//
// Presets live on the HMI (ESP32-S3 NVS via Preferences) — the controller only
// ever receives the resulting SET_TEMPERATURE / SET_WATER_LOSS commands, so no
// controller changes are needed. The classic presets (Tuyo/Danggit/Pusit) are
// seeded on first boot; users can add their own and delete any preset.

#ifndef DRYING_PRESETS_H
#define DRYING_PRESETS_H

#include <Arduino.h>

#define PRESET_NAME_MAX_LEN   13   // 12 chars + null terminator
#define PRESET_MAX_COUNT      8    // caps the on-screen list

struct DryingPreset {
    char  name[PRESET_NAME_MAX_LEN];
    float tempC;      // drying temperature setpoint (°C)
    float wlTarget;   // water-loss auto-complete target (%)
};

// Load presets from NVS (or seed the classic defaults on first boot).
// Call once from setup(), before any screen is built.
void presetsInit();

int  presetsGetCount();
const DryingPreset* presetsGet(int index);   // valid while index < presetsGetCount()

// Add a preset (empty name becomes "Preset N"). Returns false when the list is
// full or the values are out of range. Persists immediately to NVS.
bool presetsAdd(const char* name, float tempC, float wlTarget);

// Delete the preset at index; later presets shift down. Persists immediately.
bool presetsDelete(int index);

#endif // DRYING_PRESETS_H
