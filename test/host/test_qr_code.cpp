// Host unit tests for qr::QrCode and qr::formatWifiPayload (src/ui/qr_code.{h,cpp}).
#include "ui/qr_code.h"

#include <cstdio>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                  \
    do {                                                             \
        g_checks++;                                                  \
        if (!(cond)) {                                               \
            g_failed++;                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                            \
    } while (0)

// Checks the 7x7 finder pattern at top-left (0, 0) of any valid QR matrix.
static bool hasTopLeftFinder(const qr::QrCode& q) {
    if (q.size() < 21) return false;
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            const bool border = (x == 0 || x == 6 || y == 0 || y == 6);
            const bool center = (x >= 2 && x <= 4 && y >= 2 && y <= 4);
            if (q.module(x, y) != (border || center)) return false;
        }
    }
    return true;
}

int main() {
    CHECK(qr::formatWifiPayload("BookOrbit-Config", "12345678") == "WIFI:T:WPA;S:BookOrbit-Config;P:12345678;;");
    CHECK(qr::formatWifiPayload("OpenNet", "") == "WIFI:T:nopass;S:OpenNet;;");
    CHECK(qr::formatWifiPayload("My;Net:1,2\\3\"x", "a;b:c\\d") ==
          "WIFI:T:WPA;S:My\\;Net\\:1\\,2\\\\3\\\"x;P:a\\;b\\:c\\\\d;;");

    qr::QrCode wifiQr;
    String wifiStr = qr::formatWifiPayload("BookOrbit-Config", "12345678");
    CHECK(wifiQr.encode(wifiStr.c_str()));
    CHECK(wifiQr.valid());
    CHECK(wifiQr.size() >= 29 && wifiQr.size() <= 37);
    CHECK(hasTopLeftFinder(wifiQr));
    CHECK(!wifiQr.module(-1, 0));
    CHECK(!wifiQr.module(wifiQr.size(), 0));

    qr::QrCode urlQr;
    CHECK(urlQr.encode("http://192.168.4.1/"));
    CHECK(urlQr.valid());
    CHECK(urlQr.size() >= 21 && urlQr.size() <= 29);
    CHECK(hasTopLeftFinder(urlQr));

    qr::QrCode emptyQr;
    CHECK(!emptyQr.encode(""));
    CHECK(!emptyQr.valid());
    CHECK(!emptyQr.encode(nullptr));

    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
