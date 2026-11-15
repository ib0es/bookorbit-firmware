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
}  // namespace

const char* title(int kind) {
    (void)kind;
    return Appearance::label(Appearance::FULL_REFRESH);
}

int count(int kind) {
    (void)kind;
    return kRefreshCount;
}

const char* label(int kind, int index, char* buf, size_t len) {
    (void)kind;
    const int n = kRefresh[index < 0 ? 0 : (index >= kRefreshCount ? kRefreshCount - 1 : index)];
    if (n <= 0)
        snprintf(buf, len, "%s", tr(MsgId::CHOICE_REFRESH_NEVER));
    else
        snprintf(buf, len, tr(MsgId::CHOICE_REFRESH_FMT), n);
    return buf;
}

int active(const AppConfig& cfg, int kind) {
    (void)kind;
    const int cur = cfg.fullRefreshEvery <= 0 ? 0 : cfg.fullRefreshEvery;
    for (int i = 0; i < kRefreshCount; i++)
        if (kRefresh[i] == cur) return i;
    return 0;
}

void apply(AppConfig& cfg, int kind, int index) {
    (void)kind;
    if (index < 0 || index >= count(kind)) return;
    cfg.fullRefreshEvery = kRefresh[index];
}

AppState parent(int kind) {
    (void)kind;
    return STATE_READING_SETTINGS;
}

}  // namespace Choices

int orientationChoiceCount() { return BoardImu.present() ? 5 : 4; }

int orientationChoiceActive(const AppConfig& cfg) {
    const int turn = cfg.uiOrientation & 3;
    if (!BoardImu.present()) return turn;
    return cfg.autoRotate ? 0 : turn + 1;
}
