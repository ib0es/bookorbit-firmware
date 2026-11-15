// Real reader/menu rendering; only this test TU uses -fno-access-control to inspect
// pagination margins and invoke the private chrome pass without repaginating 2048 times.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "Arduino.h"
#include "InputManager.h"
#include "EInkDisplay.h"
#include "SDCardManager.h"
#include "app/app_state.h"
#include "app/touch_router.h"
#include "app/screens/screens.h"
#include "app/screens/status_bar_menu.h"
#include "platform/hal/battery.h"
#include "platform/hal/display.h"
#include "platform/hal/storage.h"
#include "platform/system.h"
#include "reader/reader.h"
#include "services/sync_clock.h"
#include "storage/config_store.h"
#include "ui/font.h"
#include "ui/i18n.h"
#include "ui/views/settings_view.h"
#include "ui_trace.h"

bool InputManager::s_simState[InputManager::BTN_COUNT] = {};
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = {};
freeink::DisplayUpdateCallback freeink::FreeInkDisplay::s_callback = nullptr;
void simPumpEvents() {}
void simInputPolled() {}

namespace {
struct Draw {
    std::string kind, text;
    uikit::Rect rect, ink;
};
std::vector<Draw> draws;
bool recording = false;
int failures = 0, checks = 0;
std::string scenario;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok && failures++ < 30) std::fprintf(stderr, "FAIL %s: %s\n", scenario.c_str(), why);
}
bool overlaps(uikit::Rect a, uikit::Rect b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}
bool inside(uikit::Rect a, uikit::Rect b) {
    return a.width > 0 && a.height > 0 && a.x >= b.x && a.y >= b.y && a.right() <= b.right() &&
           a.bottom() <= b.bottom();
}
int count(const char* kind) {
    return (int)std::count_if(draws.begin(), draws.end(), [&](const Draw& d) { return d.kind == kind; });
}
bool hasText(const std::string& value) {
    return std::any_of(draws.begin(), draws.end(), [&](const Draw& d) { return d.kind == "text" && d.text == value; });
}
bool hasInk(uikit::Rect rect) {
    const auto* fb = Display.getBuffer();
    const int pw = Display.rawWidth(), ph = Display.rawHeight();
    for (int y = std::max(0, (int)rect.y); y < std::min((int)uikit::height(), (int)rect.bottom()); ++y)
        for (int x = std::max(0, (int)rect.x); x < std::min((int)uikit::width(), (int)rect.right()); ++x) {
            int px = x, py = y;
            switch (uikit::target().orientation()) {
                case freeink::ui::Orientation::Portrait:
                    px = y;
                    py = ph - 1 - x;
                    break;
                case freeink::ui::Orientation::PortraitInverted:
                    px = pw - 1 - y;
                    py = x;
                    break;
                case freeink::ui::Orientation::LandscapeClockwise:
                    px = pw - 1 - x;
                    py = ph - 1 - y;
                    break;
                default:
                    break;
            }
            if (!(fb[py * (pw / 8) + px / 8] & (0x80 >> (px % 8)))) return true;
        }
    return false;
}

void geometry(const std::vector<Draw>& parts, uikit::Rect bounds) {
    for (size_t i = 0; i < parts.size(); ++i) {
        check(inside(parts[i].ink, bounds), "element outside its reserved status-bar/preview area");
        check(hasInk(parts[i].ink), "enabled element has no pixels in the framebuffer");
        for (size_t j = i + 1; j < parts.size(); ++j)
            check(!overlaps(parts[i].ink, parts[j].ink), "status-bar elements overlap");
    }
}
void verifyReader(int mask, bool top) {
    check(count("battery") == ((mask & SB_BATTERY) ? 1 : 0), "battery toggle not reflected in drawing");
    check(count("chapterBar") == ((mask & SB_CH_BAR) ? 1 : 0), "chapter progress toggle not reflected");
    check(count("bookBar") == ((mask & SB_BOOK_BAR) ? 1 : 0), "book progress toggle not reflected");
    const unsigned cp = Reader.curCount();
    StatusLine::Where w{Reader.getCurrentChapterIndex() + 1,
                        Reader.getChapterCount(),
                        Reader._page + 1,
                        (int)cp,
                        Reader.getCurrentPage(),
                        Reader.getTotalPages(),
                        cp ? (int)((uint32_t)(Reader._page + 1) * 100u / cp) : 0,
                        (int)(Reader.getProgress() * 100)};
    char l[48], r[48], c[160], clock[8] = "";
    SyncClock::formatLocalTime(Reader.statusBar().utcOffsetMin, clock, sizeof(clock));
    StatusLine::sides(mask, w, clock, l, sizeof(l), r, sizeof(r));
    StatusLine::center(mask, w, Reader.getTitle().c_str(), Reader.getCurrentChapterTitle().c_str(), c, sizeof(c));
    for (const char* s : {l, r, c})
        if (*s) check(hasText(s), "enabled text missing from renderer");
    auto findPart = [&](const char* kind, const char* value = "") {
        return std::find_if(draws.begin(), draws.end(),
                            [&](const Draw& d) { return d.kind == kind && (!*value || d.text == value); });
    };
    if ((mask & SB_CH_BAR) && (mask & SB_BOOK_BAR)) {
        const auto chapter = findPart("chapterBar"), book = findPart("bookBar");
        if (chapter != draws.end() && book != draws.end())
            check(top ? book->rect.y < chapter->rect.y : book->rect.y > chapter->rect.y,
                  "progress bars are not in mirrored vertical order");
    }
    if (StatusLine::centerAbove(mask) && *c && (*l || *r)) {
        const auto center = findPart("text", c), side = findPart("text", *l ? l : r);
        if (center != draws.end() && side != draws.end())
            check(top ? center->rect.y > side->rect.y : center->rect.y < side->rect.y,
                  "wrapped title is not on the text-facing row of its bar");
    }
    const int expectedTexts = !!*l + !!*r + !!*c + !!(mask & SB_BATTERY);
    check(count("text") == expectedTexts, "disabled text still rendered, or enabled text duplicated");
    std::vector<Draw> parts;
    for (const auto& d : draws)
        if (d.kind != "frame") parts.push_back(d);
    const int margin = mask == 0                          ? 16
                       : StatusLine::textLines(mask) == 2 ? 84
                       : StatusLine::textLines(mask) == 1 ? 56
                                                          : 30;
    geometry(parts, {0, (int16_t)(top ? 0 : uikit::height() - margin), uikit::width(), (int16_t)margin});
    if (*c && (*l || *r)) {
        const auto title = std::find_if(parts.begin(), parts.end(), [&](const Draw& d) { return d.text == c; });
        if (title != parts.end() && !StatusLine::centerAbove(mask)) {
            const int lw = *l ? uikit::textWidth(uikit::FontSmall, l) + 8 : 0;
            int rw = *r ? uikit::textWidth(uikit::FontSmall, r) + 4 : 0;
            if (mask & SB_BATTERY) {
                char percent[12];
                std::snprintf(percent, sizeof(percent), "%d%%", Battery.getPercentage());
                rw += uikit::textWidth(uikit::FontSmall, percent) + 36 + (*r ? 14 : 0);
            }
            check(title->rect.width >= uikit::width() - 40 - lw - rw - 16,
                  "title does not use the free width beside a single side block");
        }
    }
}
void readerMatrix() {
    using O = freeink::ui::Orientation;
    const O orientations[] = {O::Portrait, O::LandscapeClockwise, O::PortraitInverted, O::LandscapeCounterClockwise};
    for (int turn = 0; turn < 4; ++turn) {
        Reader.setOrientation(turn);
        Reader.layoutRemaining();
        Reader.setPage(2);
        Reader.renderCurrentPage();
        Reader.stopBackground();
        uikit::setOrientation(orientations[turn]);
        for (bool top : {true, false})
            for (int mask = 0; mask <= SB_ALL; ++mask) {
                scenario = "reader turn=" + std::to_string(turn) + (top ? " top=" : " bottom=") + std::to_string(mask);
                AppConfig cfg;
                cfg.sbTopItems = top ? mask : 0;
                cfg.sbBottomItems = top ? 0 : mask;
                Reader._bar = StatusBarMenu::options(cfg);
                check(Reader._bar.topItems == cfg.sbTopItems && Reader._bar.bottomItems == cfg.sbBottomItems,
                      "configuration bitmask truncated before rendering");
                uikit::clear();
                draws.clear();
                recording = true;
                Reader.drawStatusBar();
                recording = false;
                verifyReader(mask, top);
            }
    }
}
void previews() {
    using O = freeink::ui::Orientation;
    const O orientations[] = {O::Portrait, O::LandscapeClockwise, O::PortraitInverted, O::LandscapeCounterClockwise};
    for (auto lang : {AppLanguage::EN, AppLanguage::RU, AppLanguage::ES}) {
        I18n::setLanguage(lang);
        for (O orient : orientations) {
            uikit::setOrientation(orient);
            for (int mask : std::array<int, 15>{0, SB_CH_PAGES, SB_CH_PERCENT, SB_CH_BAR, SB_BOOK_PAGES,
                                                SB_BOOK_PERCENT, SB_BOOK_BAR, SB_CH_TITLE, SB_CH_NUMBER, SB_BOOK_TITLE,
                                                SB_BATTERY, SB_CLOCK, SB_TOP_DEFAULT, SB_BOTTOM_DEFAULT, SB_ALL}) {
                AppContext ctx;
                ctx.config.sbTopItems = mask;
                ctx.config.sbBottomItems = mask;
                ctx.currentState = STATE_SB_TOP;
                for (bool top : {true, false}) {
                    ctx.currentState = top ? STATE_SB_TOP : STATE_SB_BOTTOM;
                    for (int selected : {0, 10}) {
                        ctx.selectedSbTopIndex = ctx.selectedSbBottomIndex = selected;
                        scenario = "preview mask=" + std::to_string(mask) + " width=" + std::to_string(uikit::width()) +
                                   " lang=" + std::to_string((int)lang) + " selection=" + std::to_string(selected);
                        draws.clear();
                        recording = true;
                        if (top)
                            drawSbTopScreen(ctx, false);
                        else
                            drawSbBottomScreen(ctx, false);
                        recording = false;
                        const auto page = std::find_if(draws.begin(), draws.end(), [](const Draw& d) {
                            return d.kind == "frame" && d.rect.height > 60 && d.rect.width > uikit::width() / 2;
                        });
                        check(page != draws.end(), "preview missing on a list page");
                        if (page == draws.end()) continue;
                        std::vector<Draw> parts;
                        for (auto it = page + 1; it != draws.end(); ++it)
                            if (it->kind != "frame") parts.push_back(*it);
                        geometry(parts, page->rect);
                        check((int)std::count_if(parts.begin(), parts.end(),
                                                 [](const Draw& d) { return d.kind == "battery"; }) ==
                                      ((mask & SB_BATTERY) ? 2 : 0),
                              "preview does not show both battery toggles");
                        // The list's hit rectangles must not intrude into the preview.
                        for (size_t i = 0; i < uikit::hitCount(); ++i) {
                            uikit::Rect hit;
                            freeink::ui::ActionId action;
                            int16_t value;
                            uikit::hitAt(i, hit, action, value);
                            if (action == 1) check(!overlaps(hit, page->rect), "list row overlaps the preview");
                        }
                    }
                }
            }
        }
    }
}
void layoutAndLiveValues() {
    scenario = "pagination margins and no-op updates";
    const int masks[] = {0, SB_BOOK_BAR, SB_CLOCK, SB_CH_PAGES | SB_BOOK_TITLE | SB_CLOCK};
    const int margins[] = {16, 30, 56, 84};
    Reader.goTo({0, 0});
    for (int t = 0; t < 4; ++t)
        for (int b = 0; b < 4; ++b) {
            BookReader::StatusBar bar;
            bar.topItems = masks[t];
            bar.bottomItems = masks[b];
            Reader.setStatusBar(bar);
            check(Reader._params.marginTop == margins[t] && Reader._params.marginBottom == margins[b],
                  "reader pagination does not reserve the required bar space");
            const auto generation = Reader._gen;
            check(!Reader.setStatusBar(bar) && Reader._gen == generation, "unchanged options repaginate the book");
            check(Reader.position().spine == 0, "bar change loses the reading chapter");
        }
    scenario = "battery levels, timezones and missing time";
    using O = freeink::ui::Orientation;
    uikit::setOrientation(O::Portrait);
    Reader.setOrientation(0);
    Reader.layoutRemaining();
    Reader.setPage(2);
    Reader.renderCurrentPage();
    Reader.stopBackground();
    for (bool top : {true, false}) {
        for (const char* percent : {"0", "1", "82", "100"}) {
            setenv("SIM_BATTERY_PCT", percent, 1);
            for (int offset : {-720, 0, 180, 840}) {
                BookReader::StatusBar bar;
                bar.topItems = top ? SB_BATTERY | SB_CLOCK | SB_BOOK_TITLE : 0;
                bar.bottomItems = top ? 0 : SB_BATTERY | SB_CLOCK | SB_BOOK_TITLE;
                bar.utcOffsetMin = offset;
                Reader._bar = bar;
                draws.clear();
                recording = true;
                Reader.drawStatusBar();
                recording = false;
                check(hasText(std::string(percent) + "%"), "battery percentage not drawn");
                char clock[8];
                SyncClock::formatLocalTime(offset, clock, sizeof(clock));
                check(hasText(clock), "clock timezone not reflected in rendered text");
                verifyReader(SB_BATTERY | SB_CLOCK | SB_BOOK_TITLE, top);
            }
        }
        Platform::setSystemTime(1); // 0 means host clock in the simulator; 1 emulates an unset device clock.
        draws.clear();
        recording = true;
        Reader.drawStatusBar();
        recording = false;
        check(std::none_of(draws.begin(), draws.end(),
                           [](const Draw& d) { return d.text.find(':') != std::string::npos; }),
              "unset clock draws a bogus time");
        Platform::setSystemTime(1794391200);
    }
    unsetenv("SIM_BATTERY_PCT");
    // Both bars on at once: all drawing stays outside the book text rectangle.
    BookReader::StatusBar bar;
    bar.topItems = bar.bottomItems = SB_ALL;
    Reader.setStatusBar(bar);
    Reader.layoutRemaining();
    Reader.setPage(2);
    Reader.renderCurrentPage();
    Reader.stopBackground();
    draws.clear();
    recording = true;
    Reader.drawStatusBar();
    recording = false;
    const uikit::Rect body{0, Reader._params.marginTop, uikit::width(),
                           (int16_t)(uikit::height() - Reader._params.marginTop - Reader._params.marginBottom)};
    for (const auto& d : draws)
        if (d.kind != "frame") check(!overlaps(d.ink, body), "chrome overlaps book text");
    check(count("battery") == 2 && count("bookBar") == 2 && count("chapterBar") == 2,
          "both bars do not draw their own enabled elements");
    scenario = "minute redraw is not a page turn";
    const int rendered = Reader._renderedPages;
    const auto here = Reader.position();
    Platform::setSystemTime(1794391260);
    Reader.redrawForStatusBar();
    check(Reader._renderedPages == rendered && Reader.position().spine == here.spine,
          "clock redraw counts as a page turn or moves the book");
}

void configAndNavigation(const char* root) {
    scenario = "config defaults/persistence/migration";
    {
        std::ofstream out(std::string(root) + "/config.json");
        out << "{}";
    }
    AppConfig fresh;
    fresh.load();
    check(fresh.sbTopItems == SB_TOP_DEFAULT && fresh.sbBottomItems == SB_BOTTOM_DEFAULT,
          "missing keys override defaults");
    for (int bit = 1; bit <= SB_ALL; bit <<= 1) {
        fresh.sbTopItems = bit;
        fresh.sbBottomItems = SB_ALL ^ bit;
        fresh.utcOffsetMin = 180;
        check(fresh.save(), "config save failed");
        AppConfig restored;
        check(restored.load(), "config load failed");
        check(restored.sbTopItems == bit && restored.sbBottomItems == (SB_ALL ^ bit),
              "mask not preserved across reload");
        check(restored.utcOffsetMin == 180, "timezone not preserved");
    }
    {
        std::ofstream out(std::string(root) + "/config.json");
        out << R"({"sb_top":1,"sb_pages":2,"sb_clock":true,"sb_battery":false,"sb_bottom":0})";
    }
    AppConfig old;
    old.load();
    check(old.sbTopItems == (SB_CH_TITLE | SB_CLOCK), "legacy top migration changed selection");
    check(old.sbBottomItems == SB_BOTTOM_LEGACY_FULL, "legacy bottom preset changed meaning");
    AppContext ctx;
    ctx.config.sbTopItems = ctx.config.sbBottomItems = 0;
    uikit::setOrientation(freeink::ui::Orientation::Portrait);
    for (bool top : {true, false}) {
        scenario = top ? "top keyboard toggles" : "bottom keyboard toggles";
        ctx.currentState = top ? STATE_SB_TOP : STATE_SB_BOTTOM;
        auto handle = top ? SettingsScreen::handleSbTopEvent : SettingsScreen::handleSbBottomEvent;
        int& selected = top ? ctx.selectedSbTopIndex : ctx.selectedSbBottomIndex;
        int& items = top ? ctx.config.sbTopItems : ctx.config.sbBottomItems;
        for (int i = 0; i < 11; ++i) {
            const int bit = top ? sbTopItemAt(i) : sbBottomItemAt(i);
            check(bit != 0 && bit == sbTopItemAt(i) && bit == sbBottomItemAt(i), "menu item map differs between bars");
            check(selected == i, "arrow key skipped a selection");
            handle(ctx, {BTN_CONFIRM, ACTION_CLICK});
            check(items == bit, "OK did not enable exactly one item");
            handle(ctx, {BTN_CONFIRM, ACTION_CLICK});
            check(items == 0, "OK did not disable item");
            // Route an actual registered touch rectangle through TouchRouter, not
            // directly through the toggle helper. Every page must remain tappable.
            uikit::Rect target{};
            bool found = false;
            for (size_t h = 0; h < uikit::hitCount(); ++h) {
                uikit::Rect hit;
                freeink::ui::ActionId action;
                int16_t value;
                uikit::hitAt(h, hit, action, value);
                if (action == 1 && value == i) {
                    target = hit;
                    found = true;
                    break;
                }
            }
            check(found, "selected toggle has no touch hit area");
            if (found) {
                ButtonEvent translated;
                ButtonEvent tap{BTN_TAP, ACTION_CLICK, (int16_t)(target.x + target.width / 2),
                                (int16_t)(target.y + target.height / 2)};
                check(TouchRouter::translate(ctx, tap, translated), "tap did not translate to an action");
                handle(ctx, translated);
                check(items == bit, "tap did not enable exactly one item");
                check(TouchRouter::translate(ctx, tap, translated), "second tap did not translate");
                handle(ctx, translated);
                check(items == 0, "second tap did not disable item");
            }
            handle(ctx, {BTN_DOWN, ACTION_CLICK});
        }
        check(selected == 0, "DOWN does not wrap from last to first");
        handle(ctx, {BTN_UP, ACTION_CLICK});
        check(selected == 10, "UP does not wrap from first to last");
        handle(ctx, {BTN_BACK, ACTION_CLICK});
        check(ctx.currentState == STATE_STATUSBAR_MENU, "BACK returned to wrong parent");
    }
}
}  // namespace

void UiTrace::record(const char* kind, uikit::Rect rect, const char* value, uint8_t maxLines,
                     freeink::ui::FontId font) {
    if (!recording) return;
    const std::string text = value ? value : "";
    if (std::strcmp(kind, "text") == 0 && text.empty()) return;
    auto ink = rect;
    if (std::strcmp(kind, "text") == 0 && rect.width > 0) {
        const int h = maxLines == 1 ? uikit::lineHeight(font) : uikit::wrappedHeight(value, font, rect.width, maxLines);
        ink.y += (rect.height - h) / 2;
        ink.height = h;
    }
    draws.push_back({kind, text, rect, ink});
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SDCardManager::getInstance().setRoot(argv[1]);
    Display.begin();
    Font.begin(Display);
    Storage.begin();
    uikit::begin();
    Platform::setSystemTime(1794391200);
    scenario = "fixture";
    if (!Reader.openBook(argv[2])) return 2;
    Reader.setPage(2);
    Reader.renderCurrentPage();
    Reader.stopBackground();
    readerMatrix();
    previews();
    layoutAndLiveValues();
    Reader.closeBook();
    configAndNavigation(argv[1]);
    std::printf("Status bars: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
