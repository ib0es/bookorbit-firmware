#include "platform/device_profile.h"
#include "platform/devices/x4.h"
#include "platform/devices/x3.h"
#include "platform/devices/x4c.h"
#include "platform/devices/x4pro.h"
#include "platform/devices/picco.h"
#include "platform/devices/delink.h"
#include "platform/devices/papermono.h"
#include "platform/devices/sticky.h"
#include "platform/devices/ws397.h"
#include "platform/devices/metalio_eink4.h"
#include "platform/devices/murphy_m4.h"
#include "platform/devices/onepage.h"
#include "platform/devices/eego_a4.h"
#include "platform/devices/lilygo_t5s3.h"
#include "platform/devices/m5paper_v11.h"
#include "platform/devices/papers3.h"

#include <cstdio>
#include <vector>

static int g_failed = 0, g_checks = 0;
#define CHECK(cond)                                                  \
    do {                                                             \
        g_checks++;                                                  \
        if (!(cond)) {                                               \
            g_failed++;                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                            \
    } while (0)

static void validateProfile(const DeviceProfile& p) {
    printf("Checking profile: %s\n", p.name);

    // 1. touchOnly requires touch
    if (p.touchOnly) {
        CHECK(p.touch);
    }

    // 2. Devices without touch and without physical LEFT/RIGHT must have okOpensBookMenu
    if (!p.touch && !p.hasLeftRight) {
        CHECK(p.okOpensBookMenu);
    }

    // 3. Devices without physical power button must have touch (so UI can offer Sleep/PowerOff)
    if (!p.hasPowerButton) {
        CHECK(p.touch);
    }

    // 4. Power button cannot be simultaneously shared with BACK and CONFIRM
    CHECK(!(p.powerSharedWithBack && p.powerSharedWithConfirm));

    // 5. If power button is shared, hasPowerButton must be true
    if (p.powerSharedWithBack || p.powerSharedWithConfirm) {
        CHECK(p.hasPowerButton);
    }

    // 6. Devices without physical UP/DOWN must have touch (for UI scrolling/paging)
    if (!p.hasUpDown) {
        CHECK(p.touch);
    }
}

int main() {
    printf("== DeviceInputMatrix\n");
    const std::vector<const DeviceProfile*> profiles = {
        &kX4Profile,
        &kX3Profile,
        &kX4ClassicProfile,
        &kX4ProProfile,
        &kPiccoProfile,
        &kDeLinkProfile,
        &kPaperMonoProfile,
        &kStickyProfile,
        &kWs397Profile,
        &kMetalioEInk4Profile,
        &kMurphyM4Profile,
        &kOnePageProfile,
        &kEegoA4Profile,
        &kLilyGoProfile,
        &kM5PaperV11Profile,
        &kPaperS3Profile,
    };

    for (const auto* p : profiles) {
        validateProfile(*p);
    }

    if (g_failed == 0) {
        printf("OK: %d/%d checks passed\n", g_checks, g_checks);
        return 0;
    } else {
        printf("FAILED: %d failures out of %d checks\n", g_failed, g_checks);
        return 1;
    }
}
