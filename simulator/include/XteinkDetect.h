#pragma once

namespace freeink {
inline void applyXteinkDisplayController() {}
// The simulator is whatever device it was built for (-DFREEINK_DEVICE_X3=1).
inline bool selectXteinkDevice() {
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3
    return true;
#else
    return false;
#endif
}
}
