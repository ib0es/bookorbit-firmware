#include "wifi_session.h"

#include "../platform/device.h"
#include "../reader/reader.h"

namespace WifiSession {

void prepare() {
    if (!Platform::kMemory.wifiTakesReaderMemory) return;
    Reader.stopBackground();
    Reader.releaseFontCaches();
}

}
