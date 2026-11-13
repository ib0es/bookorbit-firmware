#pragma once

// Why a Wi-Fi connection did not come up, from the station's disconnect
// reasons (802.11 / ESP-IDF wifi_err_reason_t codes), so the user is told
// "wrong password" or "network not found" at once instead of waiting out
// the whole timeout for "failed to connect" (U5).

#include <cstdint>

enum class WifiFailure : uint8_t {
    None,          // still trying (or connected)
    NoNetwork,     // no access point with that name in range
    BadPassword,   // the handshake failed: wrong password (or security type)
    NoAnswer,      // nothing conclusive before the timeout
};

namespace WifiReason {
constexpr uint8_t kFourWayHandshakeTimeout = 15;
constexpr uint8_t kNoApFound = 201;
constexpr uint8_t kAuthFail = 202;
constexpr uint8_t kAssocFail = 203;
constexpr uint8_t kHandshakeTimeout = 204;
constexpr uint8_t kNoApCompatibleSecurity = 210;
constexpr uint8_t kNoApAuthmodeThreshold = 211;
constexpr uint8_t kNoApRssiThreshold = 212;
}  // namespace WifiReason

// Disconnect reasons seen during one connection attempt. A single reason can
// be a fluke (a missed beacon, a slow handshake), so most need to come twice;
// an explicit authentication failure is final at once.
struct WifiFailureTracker {
    uint8_t noNetwork = 0;
    uint8_t badPassword = 0;
    bool authFail = false;

    void reset() {
        noNetwork = 0;
        badPassword = 0;
        authFail = false;
    }

    void onDisconnect(uint8_t reason) {
        switch (reason) {
            case WifiReason::kNoApFound:
            case WifiReason::kNoApCompatibleSecurity:
            case WifiReason::kNoApAuthmodeThreshold:
            case WifiReason::kNoApRssiThreshold:
                if (noNetwork < 255) noNetwork++;
                break;
            case WifiReason::kAuthFail:
                authFail = true;
                break;
            case WifiReason::kFourWayHandshakeTimeout:
            case WifiReason::kHandshakeTimeout:
            case WifiReason::kAssocFail:
                if (badPassword < 255) badPassword++;
                break;
            default:
                break;
        }
    }

    // Give up now? None: keep waiting.
    WifiFailure verdict() const {
        if (authFail || badPassword >= 2) return WifiFailure::BadPassword;
        if (noNetwork >= 2) return WifiFailure::NoNetwork;
        return WifiFailure::None;
    }

    // At the timeout: the best guess from what was seen.
    WifiFailure atTimeout() const {
        if (badPassword > 0) return WifiFailure::BadPassword;
        if (noNetwork > 0) return WifiFailure::NoNetwork;
        return WifiFailure::NoAnswer;
    }
};
