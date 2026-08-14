// lvgl_port.cpp — host implementations of the LVGL mutex helpers that
// screen_manager.cpp uses. Declared (extern "C") by the real lvgl_v8_port.h;
// the simulator is single-threaded, so locking is a no-op.

extern "C" {

bool lvgl_port_lock(int timeout_ms) { (void)timeout_ms; return true; }
bool lvgl_port_unlock() { return true; }

} // extern "C"
