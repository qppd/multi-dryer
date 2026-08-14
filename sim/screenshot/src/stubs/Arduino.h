// Arduino.h — minimal host shim so the Multi Dryer HMI screens compile on a PC.
// Only the subset of the Arduino/ESP32 API the screens actually use is provided.

#ifndef SIM_ARDUINO_H
#define SIM_ARDUINO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

// ---- time ---------------------------------------------------------------
extern unsigned long g_sim_millis;
static inline unsigned long millis() { return g_sim_millis; }
static inline void delay(unsigned long ms) { (void)ms; }

// ---- macros ----------------------------------------------------------------
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// ---- Serial (silent on the host) -------------------------------------------
class SimSerial {
public:
    void begin(unsigned long) {}
    void print(const char*) {}
    void print(int) {}
    void print(unsigned long) {}
    void println(const char*) {}
    void println(int) {}
    void println(unsigned long) {}
    void println() {}
    void printf(const char*, ...) {}
};
extern SimSerial Serial;

// ---- String (subset used by the codebase) -----------------------------------
class String {
public:
    String() {}
    String(const char* s) { if (s) _s = s; }
    String(const String& o) { _s = o._s; }
    size_t length() const { return _s.length(); }
    const char* c_str() const { return _s.c_str(); }
    bool operator==(const char* o) const { return _s == o; }

private:
    std::string _s;
};

// ---- ESP (subset) ------------------------------------------------------------
class SimESP {
public:
    uint32_t getFreeHeap() { return 512U * 1024U; }
};
extern SimESP ESP;

#endif // SIM_ARDUINO_H
