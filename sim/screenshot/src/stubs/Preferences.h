// Preferences.h — in-memory stand-in for the ESP32 NVS API so that
// drying_presets.cpp works on the host. Storage is shared across all
// instances (a static member), mirroring NVS's cross-instance persistence.

#ifndef SIM_PREFERENCES_H
#define SIM_PREFERENCES_H

#include <stdint.h>
#include <stdio.h>
#include <string>
#include <map>
#include "Arduino.h"

class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false) { (void)ns; (void)readOnly; return true; }
    void end() {}

    uint8_t getUChar(const char* key, uint8_t def) {
        std::map<std::string, std::string>::const_iterator it = _storage.find(key);
        return it == _storage.end() ? def : (uint8_t)strtoul(it->second.c_str(), NULL, 10);
    }
    float getFloat(const char* key, float def) {
        std::map<std::string, std::string>::const_iterator it = _storage.find(key);
        return it == _storage.end() ? def : (float)strtod(it->second.c_str(), NULL);
    }
    String getString(const char* key, const char* def) {
        std::map<std::string, std::string>::const_iterator it = _storage.find(key);
        return it == _storage.end() ? String(def) : String(it->second.c_str());
    }

    void putUChar(const char* key, uint8_t v) { char b[16]; snprintf(b, sizeof(b), "%u", (unsigned)v); _storage[key] = b; }
    void putFloat(const char* key, float v)  { char b[32]; snprintf(b, sizeof(b), "%g", (double)v); _storage[key] = b; }
    void putString(const char* key, const char* v) { _storage[key] = v ? v : ""; }

private:
    inline static std::map<std::string, std::string> _storage;   // shared across instances (C++17)
};

#endif // SIM_PREFERENCES_H
