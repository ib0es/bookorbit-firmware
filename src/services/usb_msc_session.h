#pragma once

struct AppContext;

// A USB mass-storage session as the app sees it: everything that touches the
// card stops before the card is handed to the host, and the file system comes
// back afterwards (a remount on the simulator, a restart into Settings on the
// device, where the USB PHY must go back to Serial/JTAG).
namespace UsbMscSession {
bool begin(AppContext& ctx);
void exit(AppContext& ctx);
}
