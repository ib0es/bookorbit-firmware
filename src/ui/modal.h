#pragma once

// Non-blocking modal dialogs.
//
// Instead of `drawStatusModal(...); delay(1500); drawScreen();` or a
// `while (true)` loop waiting for a key, screens call Modal.showMessage() /
// Modal.showConfirm() and return immediately. AppController::loop() then:
//   * routes button events to the modal while it is open (screens see none),
//   * calls Modal.tick() to auto-close timed messages,
//   * keeps applying the usual power rules (POWER -> sleep, idle timeout).
//
// When a modal closes, its callback (if any) runs first; it should only change
// state, not draw. If the callback did not open another modal, the screen of
// the current AppState is redrawn via the hook set with setRedrawHook().

#include <Arduino.h>
#include <functional>
#include "../platform/hal/buttons.h"

// Default display time for informational messages.
constexpr uint32_t MODAL_MESSAGE_MS = 1800;

class ModalManager {
public:
    using Action = std::function<void()>;
    using RedrawHook = std::function<void(bool fullRefresh)>;

    void setRedrawHook(RedrawHook hook) { _redraw = std::move(hook); }

    // Redraws the screen of the current AppState (through the hook).
    void redrawScreen(bool fullRefresh) {
        if (_redraw) _redraw(fullRefresh);
    }

    // Message that closes after `autoCloseMs` (0 = only on a key press) or on any key.
    // `fullRedraw` selects a full e-ink refresh for the screen redrawn afterwards.
    void showMessage(const String& title, const String& line1, const String& line2 = "", const String& line3 = "",
                     const String& line4 = "", uint32_t autoCloseMs = MODAL_MESSAGE_MS, bool fullRedraw = false,
                     const String& footer = "", Action onClose = nullptr);

    // Message that stays until a key is pressed (e.g. device info).
    void showInfo(const String& title, const String& line1, const String& line2 = "", const String& line3 = "",
                  const String& line4 = "", const String& footer = "");

    // CONFIRM runs onConfirm, BACK runs onCancel; other keys are ignored.
    // On touch boards the dialog shows [cancel] [okLabel] buttons (okLabel
    // empty = "OK"); a tap elsewhere cancels.
    void showConfirm(const String& title, const String& line1, const String& line2, const String& line3,
                     const String& footer, Action onConfirm, Action onCancel = nullptr, const String& okLabel = "");

    // A panel of controls over the screen (the frontlight): `draw` paints it (it is
    // called now and by redrawPanel()), `handle` gets every event and returns
    // false to close it; then `onClose` runs and the screen is redrawn.
    using PanelDraw = std::function<void()>;
    using PanelHandle = std::function<bool(const ButtonEvent&)>;
    void showPanel(PanelDraw draw, PanelHandle handle, Action onClose = nullptr);
    void redrawPanel() {
        if (_active && _kind == Kind::Panel && _panelDraw) _panelDraw();
    }

    bool isActive() const { return _active; }

    // Remaining ms until a timed message auto-closes, or 0 if inactive/untimed.
    uint32_t remainingAutoCloseMs() const {
        if (!_active || _kind != Kind::Message || _autoCloseMs == 0) return 0;
        const uint32_t elapsed = millis() - _openedAt;
        return elapsed >= _autoCloseMs ? 1 : (_autoCloseMs - elapsed);
    }

    // Returns true if the event was consumed by an open modal.
    bool handleEvent(const ButtonEvent& event);

    // Closes timed messages whose time is up. Call every loop iteration.
    void tick();

private:
    enum class Kind { Message, Confirm, Panel };

    void open(Kind kind, const String& title, const String& l1, const String& l2, const String& l3, const String& l4,
              const String& footer);
    void close(Action callback);

    bool _active = false;
    Kind _kind = Kind::Message;
    uint32_t _openedAt = 0;
    uint32_t _autoCloseMs = 0;
    bool _fullRedraw = false;
    Action _onClose;
    Action _onConfirm;
    Action _onCancel;
    PanelDraw _panelDraw;
    PanelHandle _panelHandle;
    RedrawHook _redraw;
};

extern ModalManager Modal;
