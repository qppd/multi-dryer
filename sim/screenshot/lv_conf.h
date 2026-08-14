/**
 * @file lv_conf.h
 * LVGL 8.3 configuration for the Multi Dryer headless screenshot renderer.
 * Only the options this project actually uses are listed; everything else
 * falls back to LVGL's built-in defaults.
 */
/* clang-format off */
#if 1 /*Set it to "1" to enable content*/

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*===================
   SIZE OF THE MEMORY POOL (heap for widgets)
 *===================*/
#define LV_MEM_SIZE (192U * 1024U)

/*===================
   TICK (use lv_tick_inc() from the host app)
 *===================*/
#define LV_TICK_CUSTOM 0

/*====================
 *  LOGGING
 *====================*/
#define LV_USE_LOG 0

/*===================
   FONTS
 *===================*/
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_30 1
#define LV_FONT_MONTSERRAT_36 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/*===================
   WIDGETS
 *===================*/
#define LV_USE_LINE          1
#define LV_USE_METER         1
#define LV_USE_CHART         1
#define LV_USE_BAR           1
#define LV_USE_SLIDER        1
#define LV_USE_SWITCH        1
#define LV_USE_TABVIEW       1
#define LV_USE_CHECKBOX      1
#define LV_USE_SPINNER       1
#define LV_USE_KEYBOARD      1
#define LV_USE_TEXTAREA      1
#define LV_USE_PNG           1   // enables the bundled lodepng (used for PNG screenshots)

/*===================
   LAYOUT
 *===================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

#endif /*Set it to "1" to enable content*/
