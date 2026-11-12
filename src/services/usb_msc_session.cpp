#include "usb_msc_session.h"

#include "library_manager.h"
#include "power_service.h"
#include "sync_service.h"
#include "../app/app_state.h"
#include "../net/bookorbit_client.h"
#include "../platform/hal/storage.h"
#include "../platform/hal/usb_msc.h"
#include "../platform/system.h"
#include "../reader/reader.h"

namespace UsbMscSession {

bool begin(AppContext& ctx) {
    if (!UsbMsc.present()) return false;
    if (UsbMsc.active()) return true;

    SyncService::cancelBackground();
    BookOrbit.disconnectWiFi();
    Reader.stopBackground();
    if (Reader.isOpened()) {
        LibraryManager::getInstance().flushReaderPosition();
        Reader.closeBook();
    }
    ctx.config.save();

    if (UsbMsc.start()) return true;
    if (Platform::restartSupported()) {
        Serial.println("[USB-MSC] Restarting to recover the file system.");
        PowerService::restartIntoSettings();
    }
    Storage.begin();
    return false;
}

void exit(AppContext& ctx) {
    if (!UsbMsc.present()) return;
    UsbMsc.stop();
    if (Platform::restartSupported()) {
        // Switching the PHY back from TinyUSB OTG to USB Serial/JTAG and
        // cleanly remounting the file system takes a soft reset.
        delay(80);
        PowerService::restartIntoSettings();
    }
    Storage.begin();
    LibraryManager::getInstance().cardMayHaveChanged();
    LibraryManager::getInstance().scanLocalBooks();
    ctx.currentState = STATE_SETTINGS;
}

}
