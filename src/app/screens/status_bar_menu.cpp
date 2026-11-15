#include "status_bar_menu.h"

#include "../../core/time_utils.h"
#include "../../ui/i18n.h"

namespace StatusBarMenu {

const char* label(int row) {
    switch (row) {
        case TOP:
            return tr(MsgId::SB_TOP);
        case BOTTOM:
            return tr(MsgId::SB_BOTTOM);
        case TIMEZONE:
            return tr(MsgId::SB_TIMEZONE);
        default:
            return "";
    }
}

const char* value(const AppConfig& cfg, int row, char* buf, size_t len) {
    if (row == TIMEZONE) {
        TimeUtils::formatUtcOffset(cfg.utcOffsetMin, buf, len);
        return buf;
    }
    if (row == TOP) {
        int on = 0;
        for (int b = 0; b < NUM_SB_TOP_ITEMS; b++) on += (cfg.sbTopItems >> b) & 1;
        if (on == 0) {
            snprintf(buf, len, "%s", tr(MsgId::SB_NONE));
            return buf;
        }
        snprintf(buf, len, "%d/%d", on, NUM_SB_TOP_ITEMS);
        return buf;
    }
    if (row == BOTTOM) {
        int on = 0;
        for (int b = 0; b < NUM_SB_BOTTOM_ITEMS; b++) on += (cfg.sbBottomItems >> b) & 1;
        if (on == 0) {
            snprintf(buf, len, "%s", tr(MsgId::SB_NONE));
            return buf;
        }
        snprintf(buf, len, "%d/%d", on, NUM_SB_BOTTOM_ITEMS);
        return buf;
    }
    return "";
}

int toggleState(const AppConfig&, int) { return -1; }

void step(AppConfig& cfg, int row, int dir) {
    switch (row) {
        case TIMEZONE:
            cfg.utcOffsetMin = TimeUtils::stepUtcOffset(cfg.utcOffsetMin, dir);
            break;
        default:
            break;
    }
}

BookReader::StatusBar options(const AppConfig& cfg) {
    BookReader::StatusBar b;
    b.topItems = (uint16_t)(cfg.sbTopItems & SB_TOP_ALL);
    b.bottomItems = (uint16_t)(cfg.sbBottomItems & SB_BOTTOM_ALL);
    b.utcOffsetMin = (int16_t)TimeUtils::clampUtcOffset(cfg.utcOffsetMin);
    return b;
}

}  // namespace StatusBarMenu
