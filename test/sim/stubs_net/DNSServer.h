#pragma once
// Test stub (test/sim): the captive-portal DNS server answers nobody.
#include "WiFi.h"

enum class DNSReplyCode { NoError = 0 };

class DNSServer {
public:
    void setErrorReplyCode(DNSReplyCode) {}
    bool start(uint16_t, const String&, const IPAddress&) { return true; }
    void processNextRequest() {}
    void stop() {}
};
