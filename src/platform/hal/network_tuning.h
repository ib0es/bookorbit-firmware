#pragma once

#include <Arduino.h>

#if defined(ESP_PLATFORM) && defined(BOOKORBIT_TCP_WINDOW_EXPERIMENT)
extern "C" unsigned bookorbit_tcp_window_bytes();
#endif

namespace Platform {
inline unsigned tcpReceiveWindow() {
#if defined(ESP_PLATFORM) && defined(BOOKORBIT_TCP_WINDOW_EXPERIMENT)
    return bookorbit_tcp_window_bytes();
#elif defined(ESP_PLATFORM)
    return CONFIG_LWIP_TCP_WND_DEFAULT;
#else
    return 0;  // host simulator has no lwIP transport
#endif
}
}  // namespace Platform
