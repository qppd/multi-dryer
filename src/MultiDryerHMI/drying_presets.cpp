// drying_presets.cpp
// Multi Dryer HMI — drying preset storage (ESP32-S3 NVS via Preferences).
//
// Layout in the "presets" NVS namespace:
//   count      (uint8) — number of saved presets
//   n<i>       (string) — preset name
//   t<i>       (float)  — drying temperature setpoint °C
//   w<i>       (float)  — water-loss target %
//
// The "count" key is written the first time defaults are seeded, so its
// absence (getUChar default 0xFF) is what triggers seeding — a count of 0
// later means the user deliberately deleted every preset and must NOT be
// re-seeded.

#include "drying_presets.h"
#include <Preferences.h>

#define PRESETS_NS      "presets"
#define PRESETS_KEY_CNT "count"
#define PRESET_TEMP_MIN 30.0f
#define PRESET_TEMP_MAX 100.0f
#define PRESET_WL_MIN   10.0f
#define PRESET_WL_MAX   95.0f

static DryingPreset _presets[PRESET_MAX_COUNT];
static int          _count = 0;

// ── Persistence ───────────────────────────────────────────────────────────────
static void presetsSaveAll() {
    Preferences prefs;
    prefs.begin(PRESETS_NS, false);
    prefs.putUChar(PRESETS_KEY_CNT, (uint8_t)_count);
    char key[8];
    for (int i = 0; i < _count; i++) {
        snprintf(key, sizeof(key), "n%d", i);
        prefs.putString(key, _presets[i].name);
        snprintf(key, sizeof(key), "t%d", i);
        prefs.putFloat(key, _presets[i].tempC);
        snprintf(key, sizeof(key), "w%d", i);
        prefs.putFloat(key, _presets[i].wlTarget);
    }
    prefs.end();
}

// ── Public API ────────────────────────────────────────────────────────────────
void presetsInit() {
    _count = 0;

    Preferences prefs;
    prefs.begin(PRESETS_NS, true);
    uint8_t n = prefs.getUChar(PRESETS_KEY_CNT, 0xFF);   // 0xFF = never written
    prefs.end();

    if (n == 0xFF) {
        // First boot — seed the classic defaults so nothing is lost.
        presetsAdd("Tuyo",    60.0f, 70.0f);
        presetsAdd("Danggit", 60.0f, 70.0f);
        presetsAdd("Pusit",   50.0f, 70.0f);
        presetsSaveAll();
        return;
    }

    n = min(n, (uint8_t)PRESET_MAX_COUNT);
    char key[8];
    for (int i = 0; i < (int)n; i++) {
        snprintf(key, sizeof(key), "n%d", i);
        String name = prefs.getString(key, "");
        snprintf(key, sizeof(key), "t%d", i);
        float t = prefs.getFloat(key, 60.0f);
        snprintf(key, sizeof(key), "w%d", i);
        float w = prefs.getFloat(key, 70.0f);
        if (name.length() == 0) continue;   // skip corrupt/partial entries
        strncpy(_presets[_count].name, name.c_str(), PRESET_NAME_MAX_LEN - 1);
        _presets[_count].name[PRESET_NAME_MAX_LEN - 1] = '\0';
        _presets[_count].tempC    = constrain(t, PRESET_TEMP_MIN, PRESET_TEMP_MAX);
        _presets[_count].wlTarget = constrain(w, PRESET_WL_MIN,   PRESET_WL_MAX);
        _count++;
    }
}

int presetsGetCount() { return _count; }

const DryingPreset* presetsGet(int index) {
    if (index < 0 || index >= _count) return NULL;
    return &_presets[index];
}

bool presetsAdd(const char* name, float tempC, float wlTarget) {
    if (_count >= PRESET_MAX_COUNT) return false;
    if (tempC    < PRESET_TEMP_MIN || tempC    > PRESET_TEMP_MAX) return false;
    if (wlTarget < PRESET_WL_MIN   || wlTarget > PRESET_WL_MAX)   return false;

    DryingPreset* p = &_presets[_count];
    if (name && name[0]) {
        strncpy(p->name, name, PRESET_NAME_MAX_LEN - 1);
        p->name[PRESET_NAME_MAX_LEN - 1] = '\0';
    } else {
        snprintf(p->name, PRESET_NAME_MAX_LEN, "Preset %d", _count + 1);
    }
    p->tempC    = tempC;
    p->wlTarget = wlTarget;

    _count++;
    presetsSaveAll();
    return true;
}

bool presetsDelete(int index) {
    if (index < 0 || index >= _count) return false;
    for (int i = index; i < _count - 1; i++) _presets[i] = _presets[i + 1];
    _count--;
    presetsSaveAll();
    return true;
}
