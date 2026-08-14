# Multi Dryer — headless UI screenshot renderer.
#
# Compiles the REAL HMI screens (src/MultiDryerHMI) against LVGL 8.3 on the
# host and renders each screen to a BMP. No SDL, no display — just a memory
# framebuffer. ESP32-only code (ESP-NOW, display panel, NVS) is stubbed in
# src/stubs/.
#
# Requirements: CMake >= 3.16, a C/C++ compiler, git (to fetch LVGL).
#
# Build & run:
#   cmake -S . -B build
#   cmake --build build
#   ./build/ui_preview          (outputs BMPs into ./screenshots/)
#
# On Windows use e.g. "cmake --build build --config Release" and
# "build\Release\ui_preview.exe".

cmake_minimum_required(VERSION 3.16)
project(multi_dryer_ui_preview LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------- LVGL 8.3
include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v8.3.11
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(lvgl)

# ---------------------------------------------------------------- paths
set(SIM_ROOT  ${CMAKE_CURRENT_SOURCE_DIR})
set(STUBS_DIR ${SIM_ROOT}/src/stubs)
set(HMI_DIR   ${SIM_ROOT}/../../src/MultiDryerHMI)

# ---------------------------------------------------------------- sources
# The real HMI screens + the sim stubs (no MultiDryerHMI.ino, no real
# serial_protocol.cpp / lvgl_v8_port.cpp — those need the ESP32).
add_executable(ui_preview
    ${SIM_ROOT}/src/main.cpp
    ${STUBS_DIR}/serial_protocol.cpp
    ${STUBS_DIR}/lvgl_port.cpp
    ${HMI_DIR}/screen_manager.cpp
    ${HMI_DIR}/ui_styles.cpp
    ${HMI_DIR}/ui_optimistic_state.cpp
    ${HMI_DIR}/boot_screen.cpp
    ${HMI_DIR}/dashboard_screen.cpp
    ${HMI_DIR}/control_screen.cpp
    ${HMI_DIR}/analytics_screen.cpp
    ${HMI_DIR}/diagnostics_screen.cpp
    ${HMI_DIR}/manual_operation_screen.cpp
    ${HMI_DIR}/alert_popup.cpp
    ${HMI_DIR}/drying_presets.cpp
)

# Stubs FIRST so Arduino.h / Preferences.h / lvgl_v8_port.h resolve to the
# host shims instead of the ESP32 versions.
target_include_directories(ui_preview PRIVATE
    ${STUBS_DIR}
    ${HMI_DIR}
)

# lv_conf.h lives in this folder; LV_CONF_INCLUDE_SIMPLE makes LVGL pick it up.
target_include_directories(lvgl PUBLIC ${SIM_ROOT})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# lodepng (bundled with LVGL) — used by main.cpp to write PNG screenshots
if(TARGET lvgl)
    # LVGL's modified lodepng allocates from LVGL's own (small) memory pool by
    # default. Tell it to use external lodepng_malloc/lodepng_free instead
    # (defined in main.cpp as plain malloc/free) so full-screen PNGs fit.
    target_compile_definitions(lvgl PRIVATE LODEPNG_NO_COMPILE_ALLOCATORS)
endif()

# The HMI sources contain literal UTF-8 (degrees, em dashes, …) — MSVC needs
# /utf-8 or it misreads them under the system codepage.
if(MSVC)
    target_compile_options(ui_preview PRIVATE /utf-8)
endif()

target_link_libraries(ui_preview PRIVATE lvgl)
