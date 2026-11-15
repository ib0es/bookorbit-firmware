#include "choices.h"

#include "appearance.h"
#include "status_bar_menu.h"
#include "../../platform/hal/imu.h"
#include "../../ui/i18n.h"

namespace Choices {

namespace {
// Full refresh: every N pages, "never" after the largest (as Appearance steps them).
const int kRefresh[] = {1, 2, 3, 5, 8, 10, 15, 20, 0};
const int kRefreshCount = sizeof(kRefresh) / sizeof(kRefresh[0]);

// Power hold duration: values 5 sec / 3 sec / 2 / 1 / сразу (0).
const int kPowerHold[] = {5, 3, 2, 1, 0};
const int kPowerHoldCount = sizeof(kPowerHold) / sizeof(kPowerHold[0]);
}  // namespace

const char* title(int kind) {
    if (kind == SLEEP_HOLD) return tr(MsgId::CHOICE_TITLE_SLEEP_HOLD);
    if (kind == WAKE_HOLD) return tr(MsgId::CHOICE_TITLE_WAKE_HOLD);
    return Appearance::label(Appearance::FULL_REFRESH);
}

int count(int kind) {
    if (kind == SLEEP_HOLD || kind == WAKE_HOLD) return kPowerHoldCount;
    return kRefreshCount;
}

const char* label(int kind, int index, char* buf, size_t len) {
    if (kind == SLEEP_HOLD || kind == WAKE_HOLD) {
        const int s = kPowerHold[index < 0 ? 0 : (index >= kPowerHoldCount ? kPowerHoldCount - 1 : index)];
        if (s == 0)
            snprintf(buf, len, "%s", tr(MsgId::POWER_HOLD_IMMEDIATE));
        else
            snprintf(buf, len, tr(MsgId::POWER_HOLD_SEC_FMT), s);
        return buf;
    }
    const int n = kRefresh[index < 0 ? 0 : (index >= kRefreshCount ? kRefreshCount - 1 : index)];
    if (n <= 0)
        snprintf(buf, len, "%s", tr(MsgId::CHOICE_REFRESH_NEVER));
    else
        snprintf(buf, len, tr(MsgId::CHOICE_REFRESH_FMT), n);
    return buf;
}

int active(const AppConfig& cfg, int kind) {
    if (kind == SLEEP_HOLD) {
        for (int i = 0; i < kPowerHoldCount; i++) {
            if (kPowerHold[i] == cfg.sleepHoldSec) return i;
        }
        return kPowerHoldCount - 1;  // default to 0 (сразу)
    }
    if (kind == WAKE_HOLD) {
        for (int i = 0; i < kPowerHoldCount; i++) {
            if (kPowerHold[i] == cfg.wakeHoldSec) return i;
        }
        return 1;  // default to 3s (index 1 in {5, 3, 2, 1, 0})
    }
    const int cur = cfg.fullRefreshEvery <= 0 ? 0 : cfg.fullRefreshEvery;
    for (int i = 0; i < kRefreshCount; i++)
        if (kRefresh[i] == cur) return i;
    return 0;
}

void apply(AppConfig& cfg, int kind, int index) {
    if (index < 0 || index >= count(kind)) return;
    if (kind == SLEEP_HOLD) {
        cfg.sleepHoldSec = kPowerHold[index];
        return;
    }
    if (kind == WAKE_HOLD) {
        cfg.wakeHoldSec = kPowerHold[index];
        return;
    }
    cfg.fullRefreshEvery = kRefresh[index];
}

AppState parent(int kind) {
    if (kind == SLEEP_HOLD || kind == WAKE_HOLD) return STATE_SLEEP_SETTINGS;
    return STATE_READING_SETTINGS;
}

}  // namespace Choices

int orientationChoiceCount() { return BoardImu.present() ? 5 : 4; }

int orientationChoiceActive(const AppConfig& cfg) {
    const int turn = cfg.uiOrientation & 3;
    if (!BoardImu.present()) return turn;
    return cfg.autoRotate ? 0 : turn + 1;
}
