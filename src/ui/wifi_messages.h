#pragma once

// The line that says why Wi-Fi did not connect (U5), or `fallback` when the
// reason is not known (a cancel, an older failure).

#include "i18n.h"
#include "../core/wifi_failure.h"

inline const char* wifiFailureLine(WifiFailure f, const char* fallback) {
    switch (f) {
        case WifiFailure::NoNetwork:
            return tr(MsgId::WIFI_FAIL_NO_NETWORK);
        case WifiFailure::BadPassword:
            return tr(MsgId::WIFI_FAIL_BAD_PASSWORD);
        case WifiFailure::NoAnswer:
            return tr(MsgId::WIFI_FAIL_NO_ANSWER);
        default:
            return fallback;
    }
}
