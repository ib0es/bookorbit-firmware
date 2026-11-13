#include "qr_code.h"

#include "vendor/qrcodegen.h"
#include <cstring>

namespace qr {

static_assert(QrCode::kBufferBytes >= qrcodegen_BUFFER_LEN_FOR_VERSION(QrCode::kMaxVersion),
              "QrCode::kBufferBytes must cover kMaxVersion");

namespace {

void appendEscaped(String& out, const char* s) {
    if (!s) return;
    for (const char* p = s; *p; ++p) {
        const char c = *p;
        if (c == '\\' || c == ';' || c == ',' || c == '"' || c == ':') {
            out += '\\';
        }
        out += c;
    }
}

}  // namespace

bool QrCode::encode(const char* text) {
    size_ = 0;
    if (!text || !*text) return false;
    uint8_t temp[kBufferBytes];
    if (!qrcodegen_encodeText(text, temp, modules_, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, kMaxVersion,
                              qrcodegen_Mask_AUTO, true)) {
        return false;
    }
    size_ = qrcodegen_getSize(modules_);
    return size_ > 0;
}

bool QrCode::module(int x, int y) const {
    if (size_ <= 0 || x < 0 || y < 0 || x >= size_ || y >= size_) return false;
    return qrcodegen_getModule(modules_, x, y);
}

String formatWifiPayload(const char* ssid, const char* pass) {
    String out;
    out.reserve(64);
    const bool hasPass = pass && *pass;
    out += hasPass ? "WIFI:T:WPA;S:" : "WIFI:T:nopass;S:";
    appendEscaped(out, ssid);
    if (hasPass) {
        out += ";P:";
        appendEscaped(out, pass);
    }
    out += ";;";
    return out;
}

}  // namespace qr
