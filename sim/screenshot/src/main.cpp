// main.cpp — headless screenshot renderer for the Multi Dryer HMI.
//
// Compiles the REAL screens from src/MultiDryerHMI against LVGL 8.3, renders
// each one into an 800x480 memory framebuffer, and writes PNG files to
// <project>/docs/ui/.
//
// See ../README.md for build instructions.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include <lvgl.h>

// lodepng is compiled into the LVGL library as C (LV_USE_PNG), but LVGL's
// bundled lodepng.h is a modified decoder-only copy WITHOUT the extern "C"
// wrapper — so redeclare just the two encoder functions we use with C linkage.
extern "C" {
unsigned lodepng_encode24(unsigned char** out, size_t* outsize,
                          const unsigned char* image, unsigned w, unsigned h);
const char* lodepng_error_text(unsigned code);

// LODEPNG_NO_COMPILE_ALLOCATORS (set on the lvgl target in CMakeLists.txt)
// makes the bundled lodepng use these instead of LVGL's small memory pool.
void* lodepng_malloc(size_t size)               { return malloc(size); }
void* lodepng_realloc(void* ptr, size_t new_size) { return realloc(ptr, new_size); }
void  lodepng_free(void* ptr)                    { free(ptr); }
}

#include "ui_theme.h"
#include "ui_styles.h"
#include "dryer_data.h"
#include "screen_manager.h"
#include "alert_popup.h"
#include "drying_presets.h"

// ---------------------------------------------------------------- time
unsigned long g_sim_millis = 0;

// Global instances declared in the Arduino.h shim + dryerData (defined in the
// .ino on the ESP32, but we don't compile the .ino here)
SimSerial  Serial;
SimESP     ESP;
DryerData  dryerData;

// ---------------------------------------------------------------- display
#define SCR_W 800
#define SCR_H 480
static lv_color_t frame[SCR_W * SCR_H];      // RGB565, full-screen single buffer
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    (void)area;
    (void)color_p;   // LVGL wrote directly into `frame` (full-screen buffer)
    lv_disp_flush_ready(drv);
}

static void setup_display() {
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, frame, NULL, SCR_W * SCR_H);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCR_W;
    disp_drv.ver_res = SCR_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}

// Advance virtual time (LVGL tick + millis()) and let LVGL run its timers.
static void pump(uint32_t ms) {
    const uint32_t step = 10;
    for (uint32_t t = 0; t < ms; t += step) {
        g_sim_millis += step;
        lv_tick_inc(step);
        lv_timer_handler();
    }
}

// ---------------------------------------------------------------- PNG output
static void save_png(const char* path) {
    // Convert the RGB565 framebuffer to 24-bit RGB for lodepng
    unsigned char* rgb = (unsigned char*)malloc(SCR_W * SCR_H * 3);
    for (int y = 0; y < SCR_H; y++) {
        for (int x = 0; x < SCR_W; x++) {
            const uint16_t v = frame[y * SCR_W + x].full;   // RGB565
            uint8_t r = (v >> 11) & 0x1F; r = (r << 3) | (r >> 2);
            uint8_t g = (v >> 5)  & 0x3F; g = (g << 2) | (g >> 4);
            uint8_t b = v & 0x1F;         b = (b << 3) | (b >> 2);
            rgb[(y * SCR_W + x) * 3 + 0] = r;
            rgb[(y * SCR_W + x) * 3 + 1] = g;
            rgb[(y * SCR_W + x) * 3 + 2] = b;
        }
    }

    unsigned char* out = NULL;
    size_t outsize = 0;
    unsigned err = lodepng_encode24(&out, &outsize, rgb, SCR_W, SCR_H);
    free(rgb);

    if (err) {
        printf("!! lodepng error %u: %s\n", err, lodepng_error_text(err));
        return;
    }
    FILE* f = fopen(path, "wb");
    if (!f) { printf("!! cannot open %s\n", path); free(out); return; }
    fwrite(out, 1, outsize, f);
    fclose(f);
    free(out);
    printf("saved %s\n", path);
}

// Render the current screen and save it. Pumps >2 s so the 2 s UI-update
// timer fires once (screens fill their labels from dryerData on that tick).
// Output goes to <project>/docs/ui/ (run the binary from sim/screenshot).
static void capture(const char* name) {
    pump(2100);
    char path[200];
    snprintf(path, sizeof(path), "../../docs/ui/%s.png", name);
    save_png(path);
}

// ---------------------------------------------------------------- object search
static bool find_by_class(lv_obj_t* parent, const lv_obj_class_t* cls, lv_obj_t** out) {
    if (!parent) return false;
    uint32_t n = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* c = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(c, cls)) { *out = c; return true; }
        if (find_by_class(c, cls, out)) return true;
    }
    return false;
}

// Find a button whose label contains `needle` (used to click ADD / CLOSE).
static bool find_btn_with_text(lv_obj_t* parent, const char* needle, lv_obj_t** out) {
    if (!parent) return false;
    uint32_t n = lv_obj_get_child_cnt(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* c = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(c, &lv_btn_class)) {
            uint32_t nn = lv_obj_get_child_cnt(c);
            for (uint32_t j = 0; j < nn; j++) {
                lv_obj_t* lab = lv_obj_get_child(c, j);
                if (lv_obj_check_type(lab, &lv_label_class)) {
                    const char* txt = lv_label_get_text(lab);
                    if (txt && strstr(txt, needle)) { *out = c; return true; }
                }
            }
        }
        if (find_btn_with_text(c, needle, out)) return true;
    }
    return false;
}

// ---------------------------------------------------------------- fake data
static void set_idle() {
    initDryerData();
    dryerData.connected = true;
    dryerData.lastUpdateMs = g_sim_millis;
    dryerData.temperature = 26.4f;
    dryerData.humidity = 68.0f;
    dryerData.weight = 2.35f;
    dryerData.targetTemp = 60.0f;
    dryerData.targetWaterLoss = 70.0f;
    dryerData.sht31Detected = true;
    dryerData.tempSensorStatus = SENSOR_OK;
    dryerData.loadCellStatus = SENSOR_OK;
    dryerData.systemState = STATE_IDLE;
}

static void set_drying() {
    set_idle();
    dryerData.systemState = STATE_DRYING;
    dryerData.temperature = 57.8f;
    dryerData.humidity = 41.0f;
    dryerData.weight = 1.72f;
    dryerData.waterLoss = 26.8f;
    dryerData.heaterOn = true;
    dryerData.fanOn = true;
    dryerData.exhaustOn = false;
    dryerData.pidEnabled = true;
    dryerData.pidOutput = 1800.0f;
    dryerData.dryingElapsedMs = (5UL * 3600UL + 23UL * 60UL) * 1000UL;   // 5h23m
    dryerData.estimatedEDT = 2UL * 3600UL + 11UL * 60UL;                 // 2h11m
}

// Mirror of the 2 s UI-update timer created in MultiDryerHMI.ino
static void ui_update_timer_cb(lv_timer_t* t) {
    (void)t;
    updateCurrentScreen();
    checkAlerts();
}

// ---------------------------------------------------------------- main
int main() {
    setup_display();

    // Mirror MultiDryerHMI.ino setup
    initStyles();
    alertInit();
    presetsInit();
    screenManagerInit();
    lv_timer_create(ui_update_timer_cb, 2000, NULL);

    // Extra demo presets so the control screen row + manager list are fuller
    presetsAdd("Anchovy", 52.0f, 65.0f);
    presetsAdd("Shrimp", 55.0f, 60.0f);

    MKDIR("../../docs/ui");

    // 1. Boot (capture before the 3 s auto-transition fires)
    pump(400);
    save_png("../../docs/ui/boot.png");

    // 2. Dashboard — idle
    set_idle();
    loadScreen(SCREEN_DASHBOARD);
    capture("dashboard_idle");

    // 3. Dashboard — drying
    set_drying();
    capture("dashboard_drying");

    // 4. Control — dynamic preset row (5 presets + Custom + Add)
    loadScreen(SCREEN_CONTROL);
    capture("control");

    // 5. Control — preset manager modal open, then close it again
    {
        lv_obj_t* btn = NULL;
        if (find_btn_with_text(lv_scr_act(), "Add", &btn)) {
            lv_event_send(btn, LV_EVENT_CLICKED, NULL);
            pump(400);
            save_png("../../docs/ui/control_presets.png");

            // Dismiss the modal (its CLOSE button lives on the top layer)
            lv_obj_t* closeBtn = NULL;
            if (find_btn_with_text(lv_layer_top(), "CLOSE", &closeBtn)) {
                lv_event_send(closeBtn, LV_EVENT_CLICKED, NULL);
                pump(200);
            }
        } else {
            printf("!! ADD button not found\n");
        }
    }

    // 6. Analytics — fill the 120-point charts with a drying curve, capture each tab
    set_drying();
    loadScreen(SCREEN_ANALYTICS);
    pump(400);
    for (int i = 0; i < 125; i++) {
        dryerData.temperature = 48.0f + 0.1f * (float)i;
        dryerData.humidity = 72.0f - 0.25f * (float)i;
        dryerData.weight = 2.4f - 0.005f * (float)i;
        pump(2000);   // fires the 2 s update timer → chart gets one more point
    }
    capture("analytics_temperature");

    lv_obj_t* tv = NULL;
    if (find_by_class(lv_scr_act(), &lv_tabview_class, &tv)) {
        lv_tabview_set_act(tv, 1, LV_ANIM_OFF);
        pump(400);
        save_png("../../docs/ui/analytics_humidity.png");
        lv_tabview_set_act(tv, 2, LV_ANIM_OFF);
        pump(400);
        save_png("../../docs/ui/analytics_weight.png");
    } else {
        printf("!! analytics tabview not found\n");
    }

    // 7. Diagnostics
    set_drying();
    loadScreen(SCREEN_DIAGNOSTICS);
    capture("diagnostics");

    // 8. HOW TO USE
    loadScreen(SCREEN_MANUAL);
    capture("how_to_use");

    printf("Done — screenshots written to ./docs/ui/\n");
    return 0;
}
