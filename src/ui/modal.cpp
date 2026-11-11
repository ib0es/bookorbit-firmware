#include "modal.h"
#include "views/status_modal.h"
#include "kit/ui_kit.h"
#include "i18n.h"

ModalManager Modal;

static const char* orNull(const String& s) { return s.length() > 0 ? s.c_str() : nullptr; }

void ModalManager::open(Kind kind, const String& title, const String& l1, const String& l2,
                        const String& l3, const String& l4, const String& footer) {
    _active = true;
    _kind = kind;
    drawStatusModal(title.c_str(), l1.c_str(), orNull(l2), orNull(l3), orNull(l4), orNull(footer));
    // Timed from when it is on the panel: the refresh (~0.5 s, a full one 3.8 s)
    // no longer eats into the time a message is readable.
    _openedAt = millis();
}

void ModalManager::showMessage(const String& title, const String& line1, const String& line2,
                               const String& line3, const String& line4, uint32_t autoCloseMs,
                               bool fullRedraw, const String& footer, Action onClose) {
    _autoCloseMs = autoCloseMs;
    _fullRedraw = fullRedraw;
    _onClose = std::move(onClose);
    _onConfirm = nullptr;
    _onCancel = nullptr;
    open(Kind::Message, title, line1, line2, line3, line4, footer);
}

void ModalManager::showInfo(const String& title, const String& line1, const String& line2,
                            const String& line3, const String& line4, const String& footer) {
    String foot = footer.length() > 0 ? footer : String(tr(MsgId::MODAL_DEFAULT_BACK));
    showMessage(title, line1, line2, line3, line4, 0, false, foot);
}

void ModalManager::showConfirm(const String& title, const String& line1, const String& line2,
                               const String& line3, const String& footer,
                               Action onConfirm, Action onCancel, const String& okLabel) {
    _autoCloseMs = 0;
    _fullRedraw = false;
    _onClose = nullptr;
    _onConfirm = std::move(onConfirm);
    _onCancel = std::move(onCancel);
    _active = true;
    _kind = Kind::Confirm;
    _openedAt = millis();
    drawConfirmModal(title.c_str(), line1.c_str(), orNull(line2), orNull(line3), orNull(footer),
                     okLabel.length() ? okLabel.c_str() : tr(MsgId::MODAL_BTN_OK), tr(MsgId::MODAL_BTN_CANCEL));
}

void ModalManager::close(Action callback) {
    const bool full = _fullRedraw;
    _active = false;
    _onClose = nullptr;
    _onConfirm = nullptr;
    _onCancel = nullptr;

    if (callback) callback();
    // The callback may have opened the next modal (e.g. "deleted" after a confirm).
    if (!_active && _redraw) _redraw(full);
}

bool ModalManager::handleEvent(const ButtonEvent& event) {
    if (!_active) return false;
    if (event.action != ACTION_CLICK) return true;   // swallow non-click events too

    if (_kind == Kind::Message) {
        close(_onClose);
    } else if (event.id == BTN_CONFIRM) {
        close(_onConfirm);
    } else if (event.id == BTN_TAP) {
        // Touch: the OK button confirms; anything else (Cancel, outside) cancels.
        freeink::ui::ActionId action = 0;
        int16_t value = 0;
        if (uikit::hitTest(event.x, event.y, action, value) && action == MODAL_HIT_OK) close(_onConfirm);
        else close(_onCancel);
    } else if (event.id == BTN_BACK) {
        close(_onCancel);
    }
    return true;
}

void ModalManager::tick() {
    if (_active && _kind == Kind::Message && _autoCloseMs > 0 && millis() - _openedAt >= _autoCloseMs) {
        close(_onClose);
    }
}
