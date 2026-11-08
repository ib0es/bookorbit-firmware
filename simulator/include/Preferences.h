#pragma once
// Simulator stub of the ESP32 Preferences (NVS) API: an in-memory key/value
// store per namespace, lost when the process exits (like an erased NVS).
#include <map>
#include <string>
#include "Arduino.h"

class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false) { _ns = ns ? ns : ""; _ro = readOnly; return true; }
    void end() {}
    String getString(const char* key, const String& def = String()) {
        auto& m = store()[_ns];
        auto it = m.find(key);
        return it == m.end() ? def : String(it->second.c_str());
    }
    size_t putString(const char* key, const String& value) {
        if (_ro) return 0;
        store()[_ns][key] = value.c_str();
        return value.length();
    }
    bool remove(const char* key) { return !_ro && store()[_ns].erase(key) > 0; }
    bool clear() { if (_ro) return false; store()[_ns].clear(); return true; }
    bool isKey(const char* key) { return store()[_ns].count(key) > 0; }

private:
    static std::map<std::string, std::map<std::string, std::string>>& store() {
        static std::map<std::string, std::map<std::string, std::string>> s;
        return s;
    }
    std::string _ns;
    bool _ro = false;
};
