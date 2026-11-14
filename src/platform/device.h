#pragma once

// The profile of the board this firmware is built for (FREEINK_DEVICE_* of the
// build env). Outside src/platform nothing tests the device or the SDK's
// capability flags: it reads Platform::kDevice.

#include "device_profile.h"

#if defined(FREEINK_DEVICE_PICCO) && FREEINK_DEVICE_PICCO
#include "devices/picco.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kPiccoProfile;
}
#elif defined(FREEINK_DEVICE_DELINK) && FREEINK_DEVICE_DELINK
#include "devices/delink.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kDeLinkProfile;
}
#elif defined(FREEINK_DEVICE_PAPERMONO) && FREEINK_DEVICE_PAPERMONO
#include "devices/papermono.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kPaperMonoProfile;
}
#elif defined(FREEINK_DEVICE_STICKY) && FREEINK_DEVICE_STICKY
#include "devices/sticky.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kStickyProfile;
}
#elif defined(FREEINK_DEVICE_WS397) && FREEINK_DEVICE_WS397
#include "devices/ws397.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kWs397Profile;
}
#elif defined(FREEINK_DEVICE_METALIO_EINK4) && FREEINK_DEVICE_METALIO_EINK4
#include "devices/metalio_eink4.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kMetalioEInk4Profile;
}
#elif defined(FREEINK_DEVICE_MURPHY_M4) && FREEINK_DEVICE_MURPHY_M4
#include "devices/murphy_m4.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kMurphyM4Profile;
}
#elif defined(FREEINK_DEVICE_ONEPAGE) && FREEINK_DEVICE_ONEPAGE
#include "devices/onepage.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kOnePageProfile;
}
#elif defined(FREEINK_DEVICE_EEGO_A4) && FREEINK_DEVICE_EEGO_A4
#include "devices/eego_a4.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kEegoA4Profile;
}
#elif defined(FREEINK_DEVICE_LILYGO) && FREEINK_DEVICE_LILYGO
#include "devices/lilygo_t5s3.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kLilyGoProfile;
}
#elif defined(FREEINK_DEVICE_M5PAPER) && FREEINK_DEVICE_M5PAPER
#include "devices/m5paper_v11.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kM5PaperV11Profile;
}
#elif defined(FREEINK_DEVICE_PAPERS3) && FREEINK_DEVICE_PAPERS3
#include "devices/papers3.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kPaperS3Profile;
}
#elif defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
#include "devices/x4pro.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kX4ProProfile;
}
#elif defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC
#include "devices/x4c.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kX4ClassicProfile;
}
#elif defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3 && !(defined(FREEINK_DEVICE_X4) && FREEINK_DEVICE_X4)
#include "devices/x3.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kX3Profile;
}
#else
#include "devices/x4.h"
namespace Platform {
inline constexpr const DeviceProfile& kDevice = kX4Profile;
}
#endif

namespace Platform {
inline constexpr const MemoryTier& kMemory = *kDevice.memory;
}

// The build flags that compile driver code in (platformio.ini, the SDK's
// BoardConfig) must agree with the profile.
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
static_assert(Platform::kDevice.usbMassStorage,
              "FREEINK_CAP_USB_MSC is on: the device profile must say usbMassStorage");
#else
static_assert(!Platform::kDevice.usbMassStorage, "usbMassStorage needs FREEINK_CAP_USB_MSC in the build env");
#endif
#if defined(FREEINK_GT911_INT_WAKE) && FREEINK_GT911_INT_WAKE
static_assert(Platform::kDevice.touchWake, "FREEINK_GT911_INT_WAKE is on: the device profile must say touchWake");
#else
static_assert(!Platform::kDevice.touchWake, "touchWake needs FREEINK_GT911_INT_WAKE in the build env");
#endif
#if defined(FREEINK_CAP_TOUCH) && FREEINK_CAP_TOUCH
static_assert(Platform::kDevice.touch, "FREEINK_CAP_TOUCH is on: the device profile must say touch");
#endif
static_assert(!Platform::kDevice.touchOnly || Platform::kDevice.touch, "a touch-only board has touch");
#if defined(ESP_PLATFORM)
#if defined(BOARD_HAS_PSRAM)
static_assert(Platform::kDevice.memory == &kLargeMemory, "a board with PSRAM takes the large memory tier");
#else
static_assert(Platform::kDevice.memory == &kSmallMemory, "a board without PSRAM takes the small memory tier");
#endif
#endif
