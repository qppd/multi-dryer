#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"

#include "dryer_data.h"
#include "drying_presets.h"
#include "ui_theme.h"
#include "ui_styles.h"
#include "serial_protocol.h"
#include "screen_manager.h"
#include "alert_popup.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;

// Global dryer data instance
DryerData dryerData;

// LVGL timer for periodic UI updates
static void uiUpdateTimerCb(lv_timer_t* timer) {
    (void)timer;
    updateCurrentScreen();
    checkAlerts();
}

void setup() {
    Serial.begin(115200);
    Serial.println("=== Multi Dryer HMI ===");

    // Initialize dryer data with defaults
    initDryerData();

    // Initialize display board
    Serial.println("[HMI] Initializing display board...");
    Board *board = new Board();
    board->init();

#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif
    assert(board->begin());

    // Initialize LVGL
    Serial.println("[HMI] Initializing LVGL...");
    lvgl_port_init(board->getLCD(), board->getTouch());

    // Initialize UART communication with dryer controller
    Serial.println("[HMI] Initializing serial protocol...");
    serialProtoInit();

    // Load saved drying presets (seeds Tuyo/Danggit/Pusit on first boot)
    // NOTE: Must run AFTER board->begin() so the LCD frame buffers are allocated
    // first. presetsInit() uses NVS (flash) and some heap for Preferences; running
    // it before board init consumed heap needed by the RGB panel driver.
    presetsInit();

    // Build the UI (must hold LVGL mutex)
    Serial.println("[HMI] Building UI...");
    lvgl_port_lock(-1);

    // Initialize styles and theme
    initStyles();

    // Initialize alert system
    alertInit();

    // Initialize screen manager (creates and shows boot screen)
    screenManagerInit();

    // Create periodic timer for UI data updates (every 2 seconds)
    lv_timer_create(uiUpdateTimerCb, CHART_UPDATE_MS, NULL);

    lvgl_port_unlock();

    Serial.println("[HMI] Initialization complete!");
    Serial.printf("[HMI] Free heap: %lu KB\n", (unsigned long)(ESP.getFreeHeap() / 1024));
}

void loop() {
    // Handle UART communication with dryer controller (non-blocking)
    serialProtoUpdate();

    delay(10);
}
