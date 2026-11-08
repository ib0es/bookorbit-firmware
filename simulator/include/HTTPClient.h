#pragma once
#include "Arduino.h"
#include "WiFi.h"

class HTTPClient {
public:
    bool begin(const String&) { return true; }
    void end() {}
    int GET() { return 200; }
    String getString() { return "{}"; }
};
