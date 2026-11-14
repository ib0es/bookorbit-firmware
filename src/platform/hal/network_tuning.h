#pragma once

#include <Arduino.h>

#if defined(ESP_PLATFORM) && defined(BOOKORBIT_TCP_WINDOW_TUNING)
extern "C" __attribute__((weak)) unsigned bookorbit_tcp_window_bytes();
#endif

namespace Platform {
inline unsigned tcpReceiveWindow() {
#if defined(ESP_PLATFORM) && defined(BOOKORBIT_TCP_WINDOW_TUNING)
    if (bookorbit_tcp_window_bytes) return bookorbit_tcp_window_bytes();
#endif
#if defined(ESP_PLATFORM) && defined(CONFIG_LWIP_TCP_WND_DEFAULT)
    return CONFIG_LWIP_TCP_WND_DEFAULT;
#elif defined(ESP_PLATFORM)
    return 14360;
#else
    return 0;  // host simulator has no lwIP transport
#endif
}
}  // namespace Platform
