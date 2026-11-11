#pragma once

// The profile of the board this firmware is built for (FREEINK_DEVICE_* of the
// build env). Outside src/platform nothing tests the device or the SDK's
// capability flags: it reads Platform::kDevice.

#include "device_profile.h"

#if defined(FREEINK_DEVICE_PICCO) && FREEINK_DEVICE_PICCO
#include "devices/picco.h"
namespace Platform { inline constexpr const DeviceProfile& kDevice = kPiccoProfile; }
#elif defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
#include "devices/x4pro.h"
namespace Platform { inline constexpr const DeviceProfile& kDevice = kX4ProProfile; }
#elif defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC
#include "devices/x4c.h"
namespace Platform { inline constexpr const DeviceProfile& kDevice = kX4ClassicProfile; }
#elif defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3 && !(defined(FREEINK_DEVICE_X4) && FREEINK_DEVICE_X4)
#include "devices/x3.h"
namespace Platform { inline constexpr const DeviceProfile& kDevice = kX3Profile; }
#else
#include "devices/x4.h"
namespace Platform { inline constexpr const DeviceProfile& kDevice = kX4Profile; }
#endif

namespace Platform { inline constexpr const MemoryTier& kMemory = *kDevice.memory; }

// The build flags that compile driver code in (platformio.ini, the SDK's
// BoardConfig) must agree with the profile.
#if defined(FREEINK_CAP_USB_MSC) && FREEINK_CAP_USB_MSC
static_assert(Platform::kDevice.usbMassStorage, "FREEINK_CAP_USB_MSC is on: the device profile must say usbMassStorage");
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
