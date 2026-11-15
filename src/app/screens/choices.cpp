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

int sbRow(int kind) { return kind == SB_TOP ? StatusBarMenu::TOP : StatusBarMenu::PAGES; }
int& sbValue(AppConfig& cfg, int kind) { return kind == SB_TOP ? cfg.sbTop : cfg.sbPages; }
}  // namespace

const char* title(int kind) {
    if (kind == FULL_REFRESH) return Appearance::label(Appearance::FULL_REFRESH);
    return StatusBarMenu::label(sbRow(kind));
}

int count(int kind) { return kind == FULL_REFRESH ? kRefreshCount : 3; }

const char* label(int kind, int index, char* buf, size_t len) {
    if (kind == FULL_REFRESH) {
        const int n = kRefresh[index < 0 ? 0 : (index >= kRefreshCount ? kRefreshCount - 1 : index)];
        if (n <= 0)
            snprintf(buf, len, "%s", tr(MsgId::CHOICE_REFRESH_NEVER));
        else
            snprintf(buf, len, tr(MsgId::CHOICE_REFRESH_FMT), n);
        return buf;
    }
    AppConfig tmp;
    sbValue(tmp, kind) = index;
    return StatusBarMenu::value(tmp, sbRow(kind), buf, len);
}

int active(const AppConfig& cfg, int kind) {
    if (kind == FULL_REFRESH) {
        const int cur = cfg.fullRefreshEvery <= 0 ? 0 : cfg.fullRefreshEvery;
        for (int i = 0; i < kRefreshCount; i++)
            if (kRefresh[i] == cur) return i;
        return 0;
    }
    return sbValue(const_cast<AppConfig&>(cfg), kind);
}

void apply(AppConfig& cfg, int kind, int index) {
    if (index < 0 || index >= count(kind)) return;
    if (kind == FULL_REFRESH)
        cfg.fullRefreshEvery = kRefresh[index];
    else
        sbValue(cfg, kind) = index;
}

AppState parent(int kind) { return kind == FULL_REFRESH ? STATE_READING_SETTINGS : STATE_STATUSBAR_MENU; }

}  // namespace Choices

int orientationChoiceCount() { return BoardImu.present() ? 5 : 4; }

int orientationChoiceActive(const AppConfig& cfg) {
    const int turn = cfg.uiOrientation & 3;
    if (!BoardImu.present()) return turn;
    return cfg.autoRotate ? 0 : turn + 1;
}
