// esp_display_panel.hpp — host stand-in for the ESP32_Display_Panel library.
// The real lvgl_v8_port.h only uses these types as pointers in prototypes, so
// forward declarations are enough for it to parse on the host.

#ifndef SIM_ESP_DISPLAY_PANEL_HPP
#define SIM_ESP_DISPLAY_PANEL_HPP

namespace esp_panel {
namespace drivers {
class LCD;
class Touch;
} // namespace drivers
} // namespace esp_panel

#endif // SIM_ESP_DISPLAY_PANEL_HPP
