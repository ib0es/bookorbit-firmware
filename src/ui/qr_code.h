#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

namespace qr {

// Encodes UTF-8 payloads into a QR Code matrix using Nayuki's qrcodegen
// (stack-only buffers, no heap allocation).
class QrCode {
public:
    static constexpr int kMaxVersion = 10;
    // Matches qrcodegen_BUFFER_LEN_FOR_VERSION(10) = (((10*4+17)^2 + 7) / 8 + 1).
    static constexpr size_t kBufferBytes = 408;

    bool encode(const char* text);
    int size() const { return size_; }
    bool valid() const { return size_ > 0; }
    bool module(int x, int y) const;

private:
    uint8_t modules_[kBufferBytes]{};
    int size_ = 0;
};

// Formats a standard Wi-Fi QR payload ("WIFI:T:WPA;S:<ssid>;P:<pass>;;") with
// ZXing/iOS/Android escaping for \, ;, ,, ", and :.
String formatWifiPayload(const char* ssid, const char* pass);

}  // namespace qr
