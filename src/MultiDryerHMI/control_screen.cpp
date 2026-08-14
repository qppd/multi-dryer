// control_screen.cpp
// Fish Dryer V2 HMI - Manual control interface (industrial PLC style)

#include "control_screen.h"
#include "ui_theme.h"
#include "ui_styles.h"
#include "dryer_data.h"
#include "screen_manager.h"
#include "serial_protocol.h"
#include "ui_optimistic_state.h"
#include "drying_presets.h"

// Presets are managed dynamically from HMI NVS (see drying_presets.h) and
// rendered in a scrollable row. selectedPresetIdx = index into the saved
// preset list; -1 means Custom (manual +/− temperature row).
#define PRESET_BTN_W    110
#define PRESET_BTN_H    52

// Widget references
static lv_obj_t* tempSetLabel = NULL;
static lv_obj_t* heaterSwitch = NULL;
static lv_obj_t* fanSwitch = NULL;
static lv_obj_t* exhaustSwitch = NULL;
static lv_obj_t* autoRadio = NULL;
static lv_obj_t* manualRadio = NULL;
static lv_obj_t* waterLossSlider = NULL;
static lv_obj_t* waterLossSliderLabel = NULL;
static lv_obj_t* currentTempLabel = NULL;
static lv_obj_t* startDryingBtn = NULL;
static lv_obj_t* stopDryingBtn = NULL;
static lv_obj_t* pauseBtn = NULL;
static lv_obj_t* resumeBtn = NULL;
static lv_obj_t* presetRow = NULL;       // scrollable preset button row
static lv_obj_t* manualTempRow = NULL;   // hidden while a preset is selected

static float tempSetpoint = 60.0f;
static int   selectedPresetIdx = -1;     // -1 = Custom (manual +/− row)

// Optimistic UI state after START/STOP click so indicators/buttons react
// immediately without waiting for the next status packet from controller.
static void applyStartStopVisibility(bool showStart, bool showStop) {
    if (lv_obj_is_valid(startDryingBtn)) {
        if (showStart) lv_obj_clear_flag(startDryingBtn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(startDryingBtn, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_is_valid(stopDryingBtn)) {
        if (showStop) lv_obj_clear_flag(stopDryingBtn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(stopDryingBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void applyPauseResumeVisibility(bool showPause, bool showResume) {
    if (lv_obj_is_valid(pauseBtn)) {
        if (showPause) lv_obj_clear_flag(pauseBtn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pauseBtn, LV_OBJ_FLAG_HIDDEN);
    }
    if (lv_obj_is_valid(resumeBtn)) {
        if (showResume) lv_obj_clear_flag(resumeBtn, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(resumeBtn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void applyRelaySwitchStates(bool heaterOn, bool fanOn, bool exhaustOn) {
    if (lv_obj_is_valid(heaterSwitch)) {
        if (heaterOn) lv_obj_add_state(heaterSwitch, LV_STATE_CHECKED);
        else lv_obj_clear_state(heaterSwitch, LV_STATE_CHECKED);
    }
    if (lv_obj_is_valid(fanSwitch)) {
        if (fanOn) lv_obj_add_state(fanSwitch, LV_STATE_CHECKED);
        else lv_obj_clear_state(fanSwitch, LV_STATE_CHECKED);
    }
    if (lv_obj_is_valid(exhaustSwitch)) {
        if (exhaustOn) lv_obj_add_state(exhaustSwitch, LV_STATE_CHECKED);
        else lv_obj_clear_state(exhaustSwitch, LV_STATE_CHECKED);
    }
}

// ── Dynamic preset row ────────────────────────────────────────────────────────
// Forward declarations (defined below)
static void presetSelectCb(lv_event_t* e);
static void openPresetManagerCb(lv_event_t* e);
static void presetDeleteCb(lv_event_t* e);
static void refreshPresetUIAsync(void* arg);

static void highlightPresetButtons() {
    if (!lv_obj_is_valid(presetRow)) return;
    uint32_t cnt = lv_obj_get_child_cnt(presetRow);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t* btn = lv_obj_get_child(presetRow, i);
        if (!btn) continue;
        int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
        lv_obj_set_style_bg_color(btn, (idx == selectedPresetIdx) ? COLOR_ACCENT : COLOR_BG_BUTTON, 0);
    }
}

// Rebuild the preset button row from the saved list. Called at screen creation;
// after add/delete it runs via lv_async_call (never from inside a callback of a
// widget this function deletes).
static void rebuildPresetRow() {
    if (!lv_obj_is_valid(presetRow)) return;
    lv_obj_clean(presetRow);

    int n = presetsGetCount();
    for (int i = 0; i < n; i++) {
        const DryingPreset* p = presetsGet(i);
        char _b[32];
        // Truncate long names so the chip stays readable
        char shortName[16];
        int len = (int)strlen(p->name);
        if (len > 10) {
            memcpy(shortName, p->name, 10);
            memcpy(shortName + 10, "\xE2\x80\xA6", 3);   // "…"
            shortName[13] = '\0';
        } else {
            strcpy(shortName, p->name);
        }
        snprintf(_b, sizeof(_b), "%s\n%.0f\xC2\u00B0C", shortName, p->tempC);
        lv_obj_t* btn = createButton(presetRow, _b, PRESET_BTN_W, PRESET_BTN_H, &style_btn_nav);
        lv_obj_set_user_data(btn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(btn, presetSelectCb, LV_EVENT_CLICKED, NULL);
    }

    // Custom (manual +/− temperature) — always available
    lv_obj_t* customBtn = createButton(presetRow, "Custom", PRESET_BTN_W, PRESET_BTN_H, &style_btn_nav);
    lv_obj_set_user_data(customBtn, (void*)(intptr_t)(-1));
    lv_obj_add_event_cb(customBtn, presetSelectCb, LV_EVENT_CLICKED, NULL);

    // Add/manage presets
    lv_obj_t* addBtn = createButton(presetRow, LV_SYMBOL_PLUS " Add", PRESET_BTN_W, PRESET_BTN_H, &style_btn_primary);
    lv_obj_add_event_cb(addBtn, openPresetManagerCb, LV_EVENT_CLICKED, NULL);

    highlightPresetButtons();
}

// Preset tap (user_data = index, or -1 for Custom)
static void presetSelectCb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    selectedPresetIdx = idx;

    if (lv_obj_is_valid(manualTempRow)) {
        if (idx == -1) lv_obj_clear_flag(manualTempRow, LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(manualTempRow, LV_OBJ_FLAG_HIDDEN);
    }

    if (idx >= 0 && idx < presetsGetCount()) {
        const DryingPreset* p = presetsGet(idx);
        tempSetpoint = p->tempC;
        dryerData.targetTemp = p->tempC;
        dryerData.targetWaterLoss = p->wlTarget;

        if (lv_obj_is_valid(waterLossSlider))
            lv_slider_set_value(waterLossSlider, (int)p->wlTarget, LV_ANIM_OFF);
        if (lv_obj_is_valid(waterLossSliderLabel))
            lv_label_set_text_fmt(waterLossSliderLabel, "%d %%", (int)p->wlTarget);

        // Preset carries both values — send both so a fresh session uses them
        sendSetTemperature(p->tempC);
        sendSetWaterLoss(p->wlTarget);
    }

    if (lv_obj_is_valid(tempSetLabel)) {
        char _b[16];
        snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", tempSetpoint);
        lv_label_set_text(tempSetLabel, _b);
    }

    highlightPresetButtons();
}

// ── Preset manager modal (add + delete) ───────────────────────────────────────
static lv_obj_t* presetModalOverlay = NULL;
static lv_obj_t* presetModalList   = NULL;
static lv_obj_t* presetModalStatus = NULL;
static lv_obj_t* presetNameTa      = NULL;
static lv_obj_t* presetTempLbl     = NULL;
static lv_obj_t* presetWlLbl       = NULL;
static float presetModalTemp = 60.0f;
static float presetModalWl   = 70.0f;

// Rebuild the saved-presets list inside the modal
static void fillPresetModalList() {
    if (!lv_obj_is_valid(presetModalList)) return;
    lv_obj_clean(presetModalList);

    int n = presetsGetCount();
    if (n == 0) {
        lv_obj_t* empty = lv_label_create(presetModalList);
        lv_label_set_text(empty, "No presets yet — add one above.");
        lv_obj_set_style_text_font(empty, FONT_NORMAL, 0);
        lv_obj_set_style_text_color(empty, COLOR_TEXT_SECONDARY, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    for (int i = 0; i < n; i++) {
        const DryingPreset* p = presetsGet(i);
        lv_obj_t* row = lv_obj_create(presetModalList);
        lv_obj_set_size(row, LV_PCT(100), 40);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, COLOR_BORDER, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_hor(row, 4, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text_fmt(lbl, "%s  ·  %.0f \xC2\u00B0C  ·  %.0f%% wl", p->name, p->tempC, p->wlTarget);
        lv_obj_set_style_text_font(lbl, FONT_NORMAL, 0);
        lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);
        lv_obj_set_flex_grow(lbl, 1);

        lv_obj_t* delBtn = createButton(row, LV_SYMBOL_TRASH, 44, 30, &style_btn_danger);
        lv_obj_set_user_data(delBtn, (void*)(intptr_t)i);
        lv_obj_add_event_cb(delBtn, presetDeleteCb, LV_EVENT_CLICKED, NULL);
    }
}

// Refresh the preset row AND the modal list. Runs via lv_async_call so it never
// deletes the widget that fired the event.
static void refreshPresetUIAsync(void* arg) {
    (void)arg;
    rebuildPresetRow();
    fillPresetModalList();
}

static void presetDeleteCb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= presetsGetCount()) return;

    presetsDelete(idx);
    if (selectedPresetIdx == idx) selectedPresetIdx = -1;
    else if (selectedPresetIdx > idx) selectedPresetIdx--;

    if (lv_obj_is_valid(presetModalStatus)) lv_label_set_text(presetModalStatus, "Preset deleted.");
    lv_async_call(refreshPresetUIAsync, NULL);
}

static void presetSaveCb(lv_event_t* e) {
    (void)e;
    if (presetsGetCount() >= PRESET_MAX_COUNT) {
        if (lv_obj_is_valid(presetModalStatus)) lv_label_set_text(presetModalStatus, "Preset list is full (max 8).");
        return;
    }
    const char* name = presetNameTa ? lv_textarea_get_text(presetNameTa) : "";
    if (presetsAdd(name, presetModalTemp, presetModalWl)) {
        const DryingPreset* p = presetsGet(presetsGetCount() - 1);
        char _b[40];
        snprintf(_b, sizeof(_b), "Saved: %s", p ? p->name : "preset");
        if (lv_obj_is_valid(presetModalStatus)) lv_label_set_text(presetModalStatus, _b);
        if (lv_obj_is_valid(presetNameTa)) lv_textarea_set_text(presetNameTa, "");
        lv_async_call(refreshPresetUIAsync, NULL);
    } else {
        if (lv_obj_is_valid(presetModalStatus)) lv_label_set_text(presetModalStatus, "Could not save — check values.");
    }
}

static void presetModalTempDecCb(lv_event_t* e) {
    (void)e;
    presetModalTemp -= 1.0f;
    if (presetModalTemp < 30.0f) presetModalTemp = 30.0f;
    if (lv_obj_is_valid(presetTempLbl)) { char _b[12]; snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", presetModalTemp); lv_label_set_text(presetTempLbl, _b); }
}

static void presetModalTempIncCb(lv_event_t* e) {
    (void)e;
    presetModalTemp += 1.0f;
    if (presetModalTemp > 100.0f) presetModalTemp = 100.0f;
    if (lv_obj_is_valid(presetTempLbl)) { char _b[12]; snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", presetModalTemp); lv_label_set_text(presetTempLbl, _b); }
}

static void presetModalWlDecCb(lv_event_t* e) {
    (void)e;
    presetModalWl -= 1.0f;
    if (presetModalWl < 10.0f) presetModalWl = 10.0f;
    if (lv_obj_is_valid(presetWlLbl)) { char _b[12]; snprintf(_b, sizeof(_b), "%.0f %%", presetModalWl); lv_label_set_text(presetWlLbl, _b); }
}

static void presetModalWlIncCb(lv_event_t* e) {
    (void)e;
    presetModalWl += 1.0f;
    if (presetModalWl > 95.0f) presetModalWl = 95.0f;
    if (lv_obj_is_valid(presetWlLbl)) { char _b[12]; snprintf(_b, sizeof(_b), "%.0f %%", presetModalWl); lv_label_set_text(presetWlLbl, _b); }
}

static void closePresetModalCb(lv_event_t* e) {
    (void)e;
    if (presetModalOverlay) { lv_obj_del(presetModalOverlay); presetModalOverlay = NULL; }
    presetModalList   = NULL;
    presetModalStatus = NULL;
    presetNameTa      = NULL;
    presetTempLbl     = NULL;
    presetWlLbl       = NULL;
}

static void openPresetManagerCb(lv_event_t* e) {
    (void)e;
    if (presetModalOverlay) return;   // already open

    // Prefill from the currently selected preset, else defaults
    if (selectedPresetIdx >= 0 && selectedPresetIdx < presetsGetCount()) {
        const DryingPreset* p = presetsGet(selectedPresetIdx);
        presetModalTemp = p->tempC;
        presetModalWl   = p->wlTarget;
    } else {
        presetModalTemp = 60.0f;
        presetModalWl   = 70.0f;
    }

    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);
    presetModalOverlay = overlay;

    lv_obj_t* box = lv_obj_create(overlay);
    lv_obj_set_size(box, 460, 440);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(box, COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, COLOR_ACCENT, 0);
    lv_obj_set_style_pad_all(box, 14, 0);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Title
    lv_obj_t* title = lv_label_create(box);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  PRESET MANAGER");
    lv_obj_set_style_text_font(title, FONT_LARGE, 0);
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);

    // ── New preset form ──
    lv_obj_t* formTitle = lv_label_create(box);
    lv_label_set_text(formTitle, "NEW PRESET");
    lv_obj_set_style_text_font(formTitle, FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(formTitle, COLOR_TEXT_PRIMARY, 0);

    presetNameTa = lv_textarea_create(box);
    lv_textarea_set_one_line(presetNameTa, true);
    lv_textarea_set_max_length(presetNameTa, PRESET_NAME_MAX_LEN - 1);
    lv_textarea_set_placeholder_text(presetNameTa, "Product name (e.g. Anchovy)");
    lv_obj_set_size(presetNameTa, LV_PCT(100), 40);
    lv_obj_set_style_text_font(presetNameTa, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(presetNameTa, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_bg_color(presetNameTa, COLOR_BG_BUTTON, 0);
    lv_obj_set_style_border_color(presetNameTa, COLOR_BORDER, 0);
    lv_obj_set_style_radius(presetNameTa, 6, 0);

    // Temperature row
    lv_obj_t* tempRow = lv_obj_create(box);
    lv_obj_set_size(tempRow, LV_PCT(100), 38);
    lv_obj_set_style_bg_opa(tempRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tempRow, 0, 0);
    lv_obj_set_style_pad_all(tempRow, 0, 0);
    lv_obj_set_scrollbar_mode(tempRow, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(tempRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tempRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tempRow, 10, 0);

    lv_obj_t* tDec = createButton(tempRow, "-", 44, 34, &style_btn_nav);
    lv_obj_add_event_cb(tDec, presetModalTempDecCb, LV_EVENT_CLICKED, NULL);
    presetTempLbl = lv_label_create(tempRow);
    lv_label_set_text_fmt(presetTempLbl, "%.0f \xC2\u00B0C", presetModalTemp);
    lv_obj_set_style_text_font(presetTempLbl, FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(presetTempLbl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_flex_grow(presetTempLbl, 1);
    lv_obj_set_style_text_align(presetTempLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* tInc = createButton(tempRow, "+", 44, 34, &style_btn_nav);
    lv_obj_add_event_cb(tInc, presetModalTempIncCb, LV_EVENT_CLICKED, NULL);

    // Water-loss target row
    lv_obj_t* wlRow = lv_obj_create(box);
    lv_obj_set_size(wlRow, LV_PCT(100), 38);
    lv_obj_set_style_bg_opa(wlRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wlRow, 0, 0);
    lv_obj_set_style_pad_all(wlRow, 0, 0);
    lv_obj_set_scrollbar_mode(wlRow, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(wlRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wlRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wlRow, 10, 0);

    lv_obj_t* wDec = createButton(wlRow, "-", 44, 34, &style_btn_nav);
    lv_obj_add_event_cb(wDec, presetModalWlDecCb, LV_EVENT_CLICKED, NULL);
    presetWlLbl = lv_label_create(wlRow);
    lv_label_set_text_fmt(presetWlLbl, "%.0f %%", presetModalWl);
    lv_obj_set_style_text_font(presetWlLbl, FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(presetWlLbl, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_flex_grow(presetWlLbl, 1);
    lv_obj_set_style_text_align(presetWlLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* wInc = createButton(wlRow, "+", 44, 34, &style_btn_nav);
    lv_obj_add_event_cb(wInc, presetModalWlIncCb, LV_EVENT_CLICKED, NULL);

    // Save
    lv_obj_t* saveBtn = createButton(box, LV_SYMBOL_OK "  SAVE PRESET", LV_PCT(100), 40, &style_btn_success);
    lv_obj_add_event_cb(saveBtn, presetSaveCb, LV_EVENT_CLICKED, NULL);

    // ── Saved presets list ──
    lv_obj_t* listTitle = lv_label_create(box);
    lv_label_set_text(listTitle, "SAVED PRESETS");
    lv_obj_set_style_text_font(listTitle, FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(listTitle, COLOR_TEXT_PRIMARY, 0);

    presetModalList = lv_obj_create(box);
    lv_obj_set_size(presetModalList, LV_PCT(100), 190);
    lv_obj_set_style_bg_opa(presetModalList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(presetModalList, 1, 0);
    lv_obj_set_style_border_color(presetModalList, COLOR_BORDER, 0);
    lv_obj_set_style_radius(presetModalList, 6, 0);
    lv_obj_set_style_pad_all(presetModalList, 6, 0);
    lv_obj_set_style_pad_row(presetModalList, 4, 0);
    lv_obj_set_scrollbar_mode(presetModalList, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(presetModalList, LV_DIR_VER);
    lv_obj_set_flex_flow(presetModalList, LV_FLEX_FLOW_COLUMN);
    fillPresetModalList();   // safe: only deletes children of the modal list

    // Status line
    presetModalStatus = lv_label_create(box);
    lv_label_set_text(presetModalStatus, "");
    lv_obj_set_style_text_font(presetModalStatus, FONT_SMALL, 0);
    lv_obj_set_style_text_color(presetModalStatus, COLOR_TEXT_SECONDARY, 0);
    lv_obj_set_width(presetModalStatus, LV_PCT(100));

    // Close
    lv_obj_t* closeBtn = createButton(box, LV_SYMBOL_CLOSE "  CLOSE", LV_PCT(100), 40, &style_btn_danger);
    lv_obj_add_event_cb(closeBtn, closePresetModalCb, LV_EVENT_CLICKED, NULL);

    // Keyboard bound to the name field (shows on focus, hides on outside tap)
    lv_obj_t* kb = lv_keyboard_create(overlay);
    lv_keyboard_set_textarea(kb, presetNameTa);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
}

// Temperature +/- callbacks (only for OTHERS preset)
static void tempPlusCb(lv_event_t* e) {
    (void)e;
    if (selectedPresetIdx != -1) return;
    tempSetpoint += 1.0f;
    if (tempSetpoint > 100.0f) tempSetpoint = 100.0f;
    { char _b[12]; snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", tempSetpoint); lv_label_set_text(tempSetLabel, _b); }
    dryerData.targetTemp = tempSetpoint;
    sendSetTemperature(tempSetpoint);
}

static void tempMinusCb(lv_event_t* e) {
    (void)e;
    if (selectedPresetIdx != -1) return;
    tempSetpoint -= 1.0f;
    if (tempSetpoint < 30.0f) tempSetpoint = 30.0f;
    { char _b[12]; snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", tempSetpoint); lv_label_set_text(tempSetLabel, _b); }
    dryerData.targetTemp = tempSetpoint;
    sendSetTemperature(tempSetpoint);
}

// +5 / -5 for faster adjustment (only for OTHERS preset)
static void tempPlus5Cb(lv_event_t* e) {
    (void)e;
    if (selectedPresetIdx != -1) return;
    tempSetpoint += 5.0f;
    if (tempSetpoint > 100.0f) tempSetpoint = 100.0f;
    { char _b[12]; snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", tempSetpoint); lv_label_set_text(tempSetLabel, _b); }
    dryerData.targetTemp = tempSetpoint;
    sendSetTemperature(tempSetpoint);
}

static void tempMinus5Cb(lv_event_t* e) {
    (void)e;
    if (selectedPresetIdx != -1) return;
    tempSetpoint -= 5.0f;
    if (tempSetpoint < 30.0f) tempSetpoint = 30.0f;
    { char _b[12]; snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", tempSetpoint); lv_label_set_text(tempSetLabel, _b); }
    dryerData.targetTemp = tempSetpoint;
    sendSetTemperature(tempSetpoint);
}

// Relay toggle callbacks
static void heaterToggleCb(lv_event_t* e) {
    bool on = lv_obj_has_state(heaterSwitch, LV_STATE_CHECKED);
    sendHeaterControl(on);
}

static void fanToggleCb(lv_event_t* e) {
    bool on = lv_obj_has_state(fanSwitch, LV_STATE_CHECKED);
    sendFanControl(on);
}

static void exhaustToggleCb(lv_event_t* e) {
    bool on = lv_obj_has_state(exhaustSwitch, LV_STATE_CHECKED);
    sendExhaustControl(on);
}

// Mode radio callbacks
static void autoModeCb(lv_event_t* e) {
    (void)e;
    lv_obj_add_state(autoRadio, LV_STATE_CHECKED);
    lv_obj_clear_state(manualRadio, LV_STATE_CHECKED);
    dryerData.dryingModeAuto = true;
    // Disable manual relay controls in auto mode
    lv_obj_add_state(heaterSwitch, LV_STATE_DISABLED);
    lv_obj_add_state(fanSwitch, LV_STATE_DISABLED);
    lv_obj_add_state(exhaustSwitch, LV_STATE_DISABLED);
}

static void manualModeCb(lv_event_t* e) {
    (void)e;
    lv_obj_clear_state(autoRadio, LV_STATE_CHECKED);
    lv_obj_add_state(manualRadio, LV_STATE_CHECKED);
    dryerData.dryingModeAuto = false;
    // Enable manual relay controls
    lv_obj_clear_state(heaterSwitch, LV_STATE_DISABLED);
    lv_obj_clear_state(fanSwitch, LV_STATE_DISABLED);
    lv_obj_clear_state(exhaustSwitch, LV_STATE_DISABLED);
}

// Water loss slider callback — sends immediately so startDryingCb doesn't need to
static void waterLossSliderCb(lv_event_t* e) {
    int val = lv_slider_get_value(waterLossSlider);
    lv_label_set_text_fmt(waterLossSliderLabel, "%d %%", val);
    dryerData.targetWaterLoss = (float)val;
    sendSetWaterLoss(dryerData.targetWaterLoss);
}

// Start drying callback
static void startDryingCb(lv_event_t* e) {
    (void)e;
    // Optimistic local state (UI reacts immediately)
    dryerData.systemState = STATE_DRYING;
    dryerData.heaterOn = true;
    dryerData.fanOn = true;
    dryerData.exhaustOn = false;
    uiOptimisticSet(STATE_DRYING, true, true, false, false, true, true, false, 15000UL);
    applyRelaySwitchStates(true, true, false);
    applyStartStopVisibility(false, true);
    applyPauseResumeVisibility(true, false);

    // Temperature and water loss are already sent by preset callbacks / slider callback
    // Sending only START avoids ESP-Now packet loss from rapid-fire sends
    sendStartDrying();
    loadScreen(SCREEN_DASHBOARD);
}

// Stop drying callback
static void stopDryingCb(lv_event_t* e) {
    (void)e;
    // Optimistic local state (UI reacts immediately)
    dryerData.systemState = STATE_IDLE;
    dryerData.heaterOn = false;
    dryerData.fanOn = false;
    dryerData.exhaustOn = true;
    uiOptimisticSet(STATE_IDLE, false, false, true, true, false, false, false, 15000UL);
    applyRelaySwitchStates(false, false, true);
    applyStartStopVisibility(true, false);
    applyPauseResumeVisibility(false, false);

    sendStopDrying();
}

// Pause drying callback
static void pauseDryingCb(lv_event_t* e) {
    (void)e;
    // Optimistic local state (UI reacts immediately)
    dryerData.systemState = STATE_PAUSED;
    dryerData.heaterOn = false;
    dryerData.fanOn = false;
    dryerData.exhaustOn = false;
    uiOptimisticSet(STATE_PAUSED, false, false, false, false, true, false, true, 15000UL);
    applyRelaySwitchStates(false, false, false);
    applyStartStopVisibility(false, true);   // STOP stays available while paused
    applyPauseResumeVisibility(false, true); // hide PAUSE, show RESUME

    sendPauseDrying();
}

// Resume drying callback
static void resumeDryingCb(lv_event_t* e) {
    (void)e;
    // Optimistic local state (UI reacts immediately)
    dryerData.systemState = STATE_DRYING;
    dryerData.heaterOn = true;
    dryerData.fanOn = true;
    dryerData.exhaustOn = false;
    uiOptimisticSet(STATE_DRYING, true, true, false, false, true, true, false, 15000UL);
    applyRelaySwitchStates(true, true, false);
    applyStartStopVisibility(false, true);
    applyPauseResumeVisibility(true, false); // show PAUSE, hide RESUME

    sendResumeDrying();
}

// Helper: create a labeled switch row
static lv_obj_t* createSwitchRow(lv_obj_t* parent, const char* label, lv_obj_t** switchOut,
                                  lv_event_cb_t cb) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), 50);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(row);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, FONT_MEDIUM, 0);
    lv_obj_set_style_text_color(lbl, COLOR_TEXT_PRIMARY, 0);

    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_set_size(sw, 60, 30);
    lv_obj_set_style_bg_color(sw, COLOR_BG_BUTTON, 0);
    lv_obj_set_style_bg_color(sw, COLOR_SUCCESS, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    *switchOut = sw;

    return row;
}

lv_obj_t* createControlScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &style_screen_bg, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    // Top bar
    createTopBar(scr, "CONTROL PANEL", true);

    // Initialize setpoint from current data
    tempSetpoint = dryerData.targetTemp;

    // Scrollable content area
    lv_obj_t* content = lv_obj_create(scr);
    lv_obj_set_size(content, SCREEN_WIDTH, SCREEN_HEIGHT - TOP_BAR_HEIGHT);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, TOP_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, SIDE_PADDING, 0);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, WIDGET_SPACING, 0);
    lv_obj_set_style_pad_column(content, WIDGET_SPACING, 0);

    // ==================== LEFT COLUMN ====================
    lv_obj_t* leftCol = createCard(content, 370, 390);
    lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(leftCol, 10, 0);

    // -- Temperature setpoint section --
    lv_obj_t* tempTitle = lv_label_create(leftCol);
    lv_label_set_text(tempTitle, "Drying Preset");
    lv_obj_add_style(tempTitle, &style_text_label, 0);

    // Preset selection row (scrollable — rebuilt from saved presets)
    presetRow = lv_obj_create(leftCol);
    lv_obj_set_size(presetRow, LV_PCT(100), 58);
    lv_obj_set_style_bg_opa(presetRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(presetRow, 0, 0);
    lv_obj_set_style_pad_all(presetRow, 0, 0);
    lv_obj_set_style_pad_right(presetRow, 8, 0);
    lv_obj_set_scrollbar_mode(presetRow, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(presetRow, LV_DIR_HOR);
    lv_obj_set_flex_flow(presetRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(presetRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(presetRow, 4, 0);

    rebuildPresetRow();   // fills the row from saved presets (+ Custom, + Add)

    // Current temp display
    lv_obj_t* tempDisplay = lv_obj_create(leftCol);
    lv_obj_set_size(tempDisplay, LV_PCT(100), 55);
    lv_obj_set_style_bg_color(tempDisplay, lv_color_hex(0x141E30), 0);
    lv_obj_set_style_bg_opa(tempDisplay, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tempDisplay, 8, 0);
    lv_obj_set_style_border_width(tempDisplay, 1, 0);
    lv_obj_set_style_border_color(tempDisplay, COLOR_ACCENT, 0);
    lv_obj_set_scrollbar_mode(tempDisplay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(tempDisplay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(tempDisplay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* setLabel = lv_label_create(tempDisplay);
    lv_label_set_text(setLabel, "SET:");
    lv_obj_set_style_text_font(setLabel, FONT_SMALL, 0);
    lv_obj_set_style_text_color(setLabel, COLOR_TEXT_SECONDARY, 0);

    tempSetLabel = lv_label_create(tempDisplay);
    { char _b[12]; snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", tempSetpoint); lv_label_set_text(tempSetLabel, _b); }
    lv_obj_set_style_text_font(tempSetLabel, FONT_XL, 0);
    lv_obj_set_style_text_color(tempSetLabel, COLOR_ACCENT, 0);

    // Manual +/- controls row (hidden by default)
    manualTempRow = lv_obj_create(leftCol);
    lv_obj_set_size(manualTempRow, LV_PCT(100), 55);
    lv_obj_set_style_bg_opa(manualTempRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(manualTempRow, 0, 0);
    lv_obj_set_style_pad_all(manualTempRow, 0, 0);
    lv_obj_set_scrollbar_mode(manualTempRow, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(manualTempRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(manualTempRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(manualTempRow, 8, 0);
    lv_obj_add_flag(manualTempRow, LV_OBJ_FLAG_HIDDEN);  // Hidden until OTHERS selected

    lv_obj_t* m5Btn = createButton(manualTempRow, "-5", BTN_MIN_SIZE, BTN_MIN_SIZE, &style_btn_nav);
    lv_obj_add_event_cb(m5Btn, tempMinus5Cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* mBtn = createButton(manualTempRow, "-", BTN_MIN_SIZE, BTN_MIN_SIZE, &style_btn_nav);
    lv_obj_add_event_cb(mBtn, tempMinusCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* pBtn = createButton(manualTempRow, "+", BTN_MIN_SIZE, BTN_MIN_SIZE, &style_btn_nav);
    lv_obj_add_event_cb(pBtn, tempPlusCb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* p5Btn = createButton(manualTempRow, "+5", BTN_MIN_SIZE, BTN_MIN_SIZE, &style_btn_nav);
    lv_obj_add_event_cb(p5Btn, tempPlus5Cb, LV_EVENT_CLICKED, NULL);

    // Current temperature display
    currentTempLabel = lv_label_create(leftCol);
    { char _b[24]; snprintf(_b, sizeof(_b), "Current: %.1f \xC2\u00B0C", dryerData.temperature); lv_label_set_text(currentTempLabel, _b); }
    lv_obj_set_style_text_font(currentTempLabel, FONT_NORMAL, 0);
    lv_obj_set_style_text_color(currentTempLabel, COLOR_TEXT_SECONDARY, 0);

    // -- Relay controls --
    lv_obj_t* relaySep = lv_obj_create(leftCol);
    lv_obj_set_size(relaySep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(relaySep, COLOR_BORDER, 0);
    lv_obj_set_style_bg_opa(relaySep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(relaySep, 0, 0);

    createSwitchRow(leftCol, LV_SYMBOL_CHARGE " Heater", &heaterSwitch, heaterToggleCb);
    createSwitchRow(leftCol, LV_SYMBOL_REFRESH " Convection Fan", &fanSwitch, fanToggleCb);
    createSwitchRow(leftCol, LV_SYMBOL_UPLOAD " Exhaust Fan", &exhaustSwitch, exhaustToggleCb);

    // ==================== RIGHT COLUMN ====================
    lv_obj_t* rightCol = createCard(content, 370, 390);
    lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(rightCol, 12, 0);

    // -- Drying mode --
    lv_obj_t* modeTitle = lv_label_create(rightCol);
    lv_label_set_text(modeTitle, "Drying Mode");
    lv_obj_add_style(modeTitle, &style_text_label, 0);

    lv_obj_t* modeRow = lv_obj_create(rightCol);
    lv_obj_set_size(modeRow, LV_PCT(100), 50);
    lv_obj_set_style_bg_opa(modeRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(modeRow, 0, 0);
    lv_obj_set_style_pad_all(modeRow, 0, 0);
    lv_obj_set_scrollbar_mode(modeRow, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(modeRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(modeRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(modeRow, 20, 0);

    // Auto radio
    autoRadio = lv_checkbox_create(modeRow);
    lv_checkbox_set_text(autoRadio, " Auto");
    lv_obj_set_style_text_color(autoRadio, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(autoRadio, FONT_MEDIUM, 0);
    lv_obj_add_state(autoRadio, LV_STATE_CHECKED);
    lv_obj_add_event_cb(autoRadio, autoModeCb, LV_EVENT_CLICKED, NULL);

    // Manual radio
    manualRadio = lv_checkbox_create(modeRow);
    lv_checkbox_set_text(manualRadio, " Manual");
    lv_obj_set_style_text_color(manualRadio, COLOR_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(manualRadio, FONT_MEDIUM, 0);
    lv_obj_add_event_cb(manualRadio, manualModeCb, LV_EVENT_CLICKED, NULL);

    // -- Target water loss --
    lv_obj_t* wlSep = lv_obj_create(rightCol);
    lv_obj_set_size(wlSep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(wlSep, COLOR_BORDER, 0);
    lv_obj_set_style_bg_opa(wlSep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wlSep, 0, 0);

    lv_obj_t* wlTitle = lv_label_create(rightCol);
    lv_label_set_text(wlTitle, "Target Water Loss");
    lv_obj_add_style(wlTitle, &style_text_label, 0);

    waterLossSlider = lv_slider_create(rightCol);
    lv_obj_set_width(waterLossSlider, LV_PCT(90));
    lv_slider_set_range(waterLossSlider, 10, 95);
    lv_slider_set_value(waterLossSlider, (int)dryerData.targetWaterLoss, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(waterLossSlider, COLOR_BG_BUTTON, 0);
    lv_obj_set_style_bg_color(waterLossSlider, COLOR_IDLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(waterLossSlider, COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(waterLossSlider, 6, LV_PART_KNOB);
    lv_obj_add_event_cb(waterLossSlider, waterLossSliderCb, LV_EVENT_VALUE_CHANGED, NULL);

    waterLossSliderLabel = lv_label_create(rightCol);
    lv_label_set_text_fmt(waterLossSliderLabel, "%d %%", (int)dryerData.targetWaterLoss);
    lv_obj_set_style_text_font(waterLossSliderLabel, FONT_XL, 0);
    lv_obj_set_style_text_color(waterLossSliderLabel, COLOR_TEXT_PRIMARY, 0);

    // -- Start drying button --
    lv_obj_t* btnSep = lv_obj_create(rightCol);
    lv_obj_set_size(btnSep, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(btnSep, COLOR_BORDER, 0);
    lv_obj_set_style_bg_opa(btnSep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btnSep, 0, 0);

    // Spacer
    lv_obj_t* spacer = lv_obj_create(rightCol);
    lv_obj_set_size(spacer, 1, 10);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    startDryingBtn = createButton(rightCol, LV_SYMBOL_PLAY "  START DRYING", LV_PCT(100), 55, &style_btn_success);
    lv_obj_add_event_cb(startDryingBtn, startDryingCb, LV_EVENT_CLICKED, NULL);

    stopDryingBtn = createButton(rightCol, LV_SYMBOL_STOP "  STOP DRYING", LV_PCT(100), 55, &style_btn_danger);
    lv_obj_add_event_cb(stopDryingBtn, stopDryingCb, LV_EVENT_CLICKED, NULL);

    // Pause / Resume row (two half-width buttons, hidden until DRYING/PAUSED)
    lv_obj_t* pauseRow = lv_obj_create(rightCol);
    lv_obj_set_size(pauseRow, LV_PCT(100), 55);
    lv_obj_set_style_bg_opa(pauseRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pauseRow, 0, 0);
    lv_obj_set_style_pad_all(pauseRow, 0, 0);
    lv_obj_set_scrollbar_mode(pauseRow, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(pauseRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pauseRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    pauseBtn = createButton(pauseRow, LV_SYMBOL_PAUSE "  PAUSE", LV_PCT(48), 55, &style_btn_nav);
    lv_obj_add_event_cb(pauseBtn, pauseDryingCb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(pauseBtn, LV_OBJ_FLAG_HIDDEN);

    resumeBtn = createButton(pauseRow, LV_SYMBOL_PLAY "  RESUME", LV_PCT(48), 55, &style_btn_success);
    lv_obj_add_event_cb(resumeBtn, resumeDryingCb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(resumeBtn, LV_OBJ_FLAG_HIDDEN);

    // Set initial mode state (Auto = relay switches disabled)
    if (dryerData.dryingModeAuto) {
        lv_obj_add_state(heaterSwitch, LV_STATE_DISABLED);
        lv_obj_add_state(fanSwitch, LV_STATE_DISABLED);
        lv_obj_add_state(exhaustSwitch, LV_STATE_DISABLED);
    }

    return scr;
}

void updateControlScreen() {
    if (!currentTempLabel) return;

    // Sync with dryer data
    tempSetpoint = dryerData.targetTemp;
    
    if (lv_obj_is_valid(tempSetLabel)) {
        { char _b[12]; snprintf(_b, sizeof(_b), "%.0f \xC2\u00B0C", tempSetpoint); lv_label_set_text(tempSetLabel, _b); }
    }
    
    if (lv_obj_is_valid(currentTempLabel)) {
        { char _b[24]; snprintf(_b, sizeof(_b), "Current: %.1f \xC2\u00B0C", dryerData.temperature); lv_label_set_text(currentTempLabel, _b); }
    }

    // Use optimistic UI briefly after local START/STOP actions.
    // This prevents waiting for state relay from the controller.
    bool optimisticActive = uiOptimisticIsActive();

    // ---- START/STOP/PAUSE/RESUME visibility + Relay indicators ----
    if (optimisticActive) {
        applyStartStopVisibility(gUiOptimisticState.showStart, gUiOptimisticState.showStop);
        applyPauseResumeVisibility(gUiOptimisticState.showPause, gUiOptimisticState.showResume);
        applyRelaySwitchStates(gUiOptimisticState.heaterOn, gUiOptimisticState.fanOn, gUiOptimisticState.exhaustOn);
    } else {
        switch (dryerData.systemState) {
            case STATE_IDLE:
            case STATE_COMPLETE:
                applyStartStopVisibility(true, false);
                applyPauseResumeVisibility(false, false);
                break;
            case STATE_DRYING:
                applyStartStopVisibility(false, true);
                applyPauseResumeVisibility(true, false);
                break;
            case STATE_PAUSED:
                applyStartStopVisibility(false, true);
                applyPauseResumeVisibility(false, true);   // show RESUME
                break;
            case STATE_ERROR:
                applyStartStopVisibility(false, false);
                applyPauseResumeVisibility(false, false);
                break;
            default:
                applyStartStopVisibility(true, false);
                applyPauseResumeVisibility(false, false);
                break;
        }
        applyRelaySwitchStates(dryerData.heaterOn, dryerData.fanOn, dryerData.exhaustOn);
    }
}
