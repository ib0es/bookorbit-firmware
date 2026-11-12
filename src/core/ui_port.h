#pragma once

// What the layers below the UI (reader, services, net) may ask of the screen,
// without drawing themselves (A5): a status dialog, a timed message, the
// current screen redrawn, the sleep and web-portal screens. The UI fills the
// port at start-up (app/ui_port_impl.cpp); until then, and in host tests,
// every call does nothing.

#include <cstdint>

struct SleepScreen {
    bool withCover = false;        // Settings: the book's cover on the sleep screen
    const char* coverPath = "";    // "" when there is none
    bool coverCrop = false;        // crop to fill (else scale to fit)
    const char* title = "";
    const char* author = "";
    float progress = 0.0f;         // 0..1, 0 when unknown
};

enum class PortalAction : uint8_t {
    Stay = 0,
    Redraw,
    Close,
};

struct UiPort {
    void (*status)(const char* title, const char* l1, const char* l2, const char* l3, const char* l4,
                   const char* footer, bool fullRefresh) = nullptr;
    void (*message)(const char* title, const char* l1, const char* l2, const char* l3, const char* l4,
                    uint32_t autoCloseMs, bool fullRedraw, const char* footer) = nullptr;
    void (*redrawScreen)(bool fullRefresh) = nullptr;
    bool (*modalActive)() = nullptr;
    uint32_t (*modalRemainingMs)() = nullptr;
    void (*coverCacheChanged)() = nullptr;           // covers on the card changed: drop decoded ones
    void (*sleepScreen)(const SleepScreen& s) = nullptr;   // drawn, not yet on the panel
    void (*portalScreen)(const char* apSsid, const char* apPass, const char* url, int page, bool fullRefresh) = nullptr;
    PortalAction (*portalEvent)(int btnId, int btnAction, int16_t x, int16_t y, int& page) = nullptr;
};

namespace Ui {

inline UiPort& port() {
    static UiPort p;
    return p;
}

inline void status(const char* title, const char* l1, const char* l2 = nullptr, const char* l3 = nullptr,
                   const char* l4 = nullptr, const char* footer = nullptr, bool fullRefresh = false) {
    if (port().status) port().status(title, l1, l2, l3, l4, footer, fullRefresh);
}
inline void message(const char* title, const char* l1, const char* l2 = "", const char* l3 = "", const char* l4 = "",
                    uint32_t autoCloseMs = 1800, bool fullRedraw = false, const char* footer = "") {
    if (port().message) port().message(title, l1, l2, l3, l4, autoCloseMs, fullRedraw, footer);
}
inline void redrawScreen(bool fullRefresh) { if (port().redrawScreen) port().redrawScreen(fullRefresh); }
inline bool modalActive() { return port().modalActive && port().modalActive(); }
inline uint32_t modalRemainingMs() { return port().modalRemainingMs ? port().modalRemainingMs() : 0; }
inline void coverCacheChanged() { if (port().coverCacheChanged) port().coverCacheChanged(); }
inline void sleepScreen(const SleepScreen& s) { if (port().sleepScreen) port().sleepScreen(s); }
inline void portalScreen(const char* apSsid, const char* apPass, const char* url, int page = 0, bool fullRefresh = false) {
    if (port().portalScreen) port().portalScreen(apSsid, apPass, url, page, fullRefresh);
}
inline PortalAction portalEvent(int btnId, int btnAction, int16_t x, int16_t y, int& page) {
    return port().portalEvent ? port().portalEvent(btnId, btnAction, x, y, page) : PortalAction::Stay;
}

}  // namespace Ui
