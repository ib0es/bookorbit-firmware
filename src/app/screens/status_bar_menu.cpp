#include "status_bar_menu.h"

#include "../../core/time_utils.h"
#include "../../ui/i18n.h"

namespace StatusBarMenu {

const char* label(int row) {
    switch (row) {
        case TOP:
            return tr(MsgId::SB_TOP);
        case PAGES:
            return tr(MsgId::SB_PAGES);
        case PERCENT:
            return tr(MsgId::SB_PERCENT);
        case BATTERY:
            return tr(MsgId::SB_BATTERY);
        case BOTTOM:
            return tr(MsgId::SB_BOTTOM);
        case CLOCK:
            return tr(MsgId::SB_CLOCK);
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
    MsgId id = MsgId::SB_NONE;
    switch (row) {
        case TOP:
            id = cfg.sbTop == 0 ? MsgId::SB_TOP_BOOK : cfg.sbTop == 1 ? MsgId::SB_TOP_CHAPTER : MsgId::SB_NONE;
            break;
        case PAGES:
            id = cfg.sbPages == 0 ? MsgId::SB_PAGES_BOOK : cfg.sbPages == 1 ? MsgId::SB_PAGES_CHAPTER : MsgId::SB_NONE;
            break;
        case PERCENT:
            id = cfg.sbPercent ? MsgId::APPEAR_ON : MsgId::APPEAR_OFF;
            break;
        case BATTERY:
            id = cfg.sbBattery ? MsgId::APPEAR_ON : MsgId::APPEAR_OFF;
            break;
        case BOTTOM: {   // how many of its items are on ("3/6"), or "None"
            int on = 0;
            for (int b = 0; b < NUM_SB_BOTTOM_ITEMS; b++) on += (cfg.sbBottomItems >> b) & 1;
            if (on == 0) break;   // SB_NONE
            snprintf(buf, len, "%d/%d", on, NUM_SB_BOTTOM_ITEMS);
            return buf;
        }
        case CLOCK:
            id = cfg.sbClock ? MsgId::APPEAR_ON : MsgId::APPEAR_OFF;
            break;
        default:
            break;
    }
    snprintf(buf, len, "%s", tr(id));
    return buf;
}

int toggleState(const AppConfig& cfg, int row) {
    switch (row) {
        case PERCENT:
            return cfg.sbPercent ? 1 : 0;
        case BATTERY:
            return cfg.sbBattery ? 1 : 0;
        case CLOCK:
            return cfg.sbClock ? 1 : 0;
        default:
            return -1;
    }
}

void step(AppConfig& cfg, int row, int dir) {
    auto three = [&](int& v) { v = (v + (dir > 0 ? 1 : 2)) % 3; };
    switch (row) {
        case TOP:
            three(cfg.sbTop);
            break;
        case PAGES:
            three(cfg.sbPages);
            break;
        case PERCENT:
            cfg.sbPercent = !cfg.sbPercent;
            break;
        case BATTERY:
            cfg.sbBattery = !cfg.sbBattery;
            break;
        case BOTTOM:   // a screen of its own (switches): nothing to step here
            break;
        case CLOCK:
            cfg.sbClock = !cfg.sbClock;
            break;
        case TIMEZONE:
            cfg.utcOffsetMin = TimeUtils::stepUtcOffset(cfg.utcOffsetMin, dir);
            break;
        default:
            break;
    }
}

BookReader::StatusBar options(const AppConfig& cfg) {
    BookReader::StatusBar b;
    b.top = (uint8_t)cfg.sbTop;
    b.pages = (uint8_t)cfg.sbPages;
    b.percent = cfg.sbPercent;
    b.battery = cfg.sbBattery;
    b.clock = cfg.sbClock;
    b.utcOffsetMin = (int16_t)TimeUtils::clampUtcOffset(cfg.utcOffsetMin);
    b.bottomItems = (uint8_t)(cfg.sbBottomItems & SB_BOTTOM_ALL);
    return b;
}

}  // namespace StatusBarMenu
