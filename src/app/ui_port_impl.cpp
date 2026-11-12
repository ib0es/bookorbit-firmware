#include "ui_port_impl.h"

#include "../core/ui_port.h"
#include "../ui/jpeg_viewer.h"
#include "../ui/modal.h"
#include "../ui/views/portal_view.h"
#include "../ui/views/sleep_view.h"
#include "../ui/views/status_modal.h"

namespace UiPortImpl {
namespace {
const char* orEmpty(const char* s) { return s ? s : ""; }
}

void install() {
    UiPort& p = Ui::port();
    p.status = [](const char* title, const char* l1, const char* l2, const char* l3, const char* l4,
                  const char* footer, bool fullRefresh) {
        drawStatusModal(title, l1, l2, l3, l4, footer, fullRefresh);
    };
    p.message = [](const char* title, const char* l1, const char* l2, const char* l3, const char* l4,
                   uint32_t autoCloseMs, bool fullRedraw, const char* footer) {
        Modal.showMessage(orEmpty(title), orEmpty(l1), orEmpty(l2), orEmpty(l3), orEmpty(l4), autoCloseMs, fullRedraw,
                          orEmpty(footer));
    };
    p.redrawScreen = [](bool fullRefresh) { Modal.redrawScreen(fullRefresh); };
    p.modalActive = []() { return Modal.isActive(); };
    p.modalRemainingMs = []() { return Modal.remainingAutoCloseMs(); };
    p.coverCacheChanged = []() { JpegViewer::clearCache(); };
    p.sleepScreen = [](const SleepScreen& s) { drawSleepScreen(s); };
    p.portalScreen = [](const char* apPass) { drawPortalScreen(apPass); };
}

}  // namespace UiPortImpl
