#include <algorithm>
#include <cmath>
#include "home_view.h"
#include "../../platform/hal/display.h"
#include "../../ui/jpeg_viewer.h"
#include "../../services/library_manager.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"

using namespace uikit;

namespace {

enum HomeAction : fi::ActionId { ActionContinue = 1, ActionCategory, ActionSync };

// Featured "continue reading" card: cover on the left, title, author, progress.
void drawFeatured(Frame& f, Rect card, AppContext& ctx, const RecentBookInfo& book, int index, int total) {
    const bool selected = (ctx.selectedHomeIndex == 0);
    if (selected) {
        frameRect(insetXY(card, -4, -4), Color::Black, 2);
    }
    f.hit(card, ActionContinue);

    const int16_t indicatorH = (int16_t)(lineHeight(FontSmall) + 8);
    const int16_t coverH = (int16_t)(card.height - indicatorH);
    // At most half the card: in landscape the card is narrow and tall, and the
    // text column beside the cover had no room left (the cover is cropped to fit).
    const int16_t coverW = (int16_t)std::min<int>(coverH * 2 / 3 + 4, card.width / 2);
    Rect cover{card.x, card.y, coverW, coverH};
    frameRect(cover, Color::Black);

    bool coverDrawn = false;
    // P4: book.coverPath is LibraryManager::recentCover()'s answer, kept until the
    // library changes; an empty one means "no cover": the card is not searched
    // again (about 12 file checks) at every key press.
    const String& covPath = book.coverPath;
    if (covPath.length() > 0) {
        coverDrawn = JpegViewer::drawCover(covPath.c_str(), cover.x + 1, cover.y + 1, cover.width - 2, cover.height - 2,
                                           COVER_FIT_CROP);
    }
    if (!coverDrawn) {
        JpegViewer::drawCoverPlaceholder(book.title.c_str(), book.author.c_str(), cover.x + 1, cover.y + 1,
                                         cover.width - 2, cover.height - 2);
    }

    char carBuf[32];
    if (total > 0) snprintf(carBuf, sizeof(carBuf), tr(MsgId::HOME_CAROUSEL_FMT), index + 1, total);
    else snprintf(carBuf, sizeof(carBuf), "%s", tr(MsgId::HOME_CAROUSEL_EMPTY));
    text(Rect{cover.x, (int16_t)(cover.bottom() + 4), cover.width, (int16_t)(indicatorH - 4)}, carBuf, FontSmall,
         Color::Black, TextAlign::Center);

    // Text column.
    Rect info{(int16_t)(cover.right() + 18), card.y, (int16_t)(card.right() - cover.right() - 18), card.height};
    const int16_t smallH = lineHeight(FontSmall);
    Rect top = takeTop(info, (int16_t)(smallH + 10));
    const char* readLabel = tr(MsgId::HOME_READ_BTN);
    const int16_t btnW = (int16_t)(pillWidth(readLabel) + 16);
    if (selected) {
        Rect btn{(int16_t)(top.right() - btnW), top.y, btnW, (int16_t)(smallH + 8)};
        fill(btn, Color::Black, 4);
        text(btn, readLabel, FontSmall, Color::White, TextAlign::Center);
    }
    text(Rect{top.x, top.y, (int16_t)(top.width - btnW - kGap), top.height}, tr(MsgId::HOME_READING_NOW), FontSmall);

    takeTop(info, 4);
    const int16_t titleH = wrappedHeight(book.title.c_str(), FontTitle, info.width, 3);
    text(takeTop(info, titleH), book.title.c_str(), FontTitle, Color::Black, TextAlign::Left, 3);
    if (book.author.length() > 0) {
        takeTop(info, 6);
        const int16_t authorH = wrappedHeight(book.author.c_str(), FontBody, info.width, 2);
        text(takeTop(info, authorH), book.author.c_str(), FontBody, Color::Black, TextAlign::Left, 2);
    }

    // Progress bar + percentage at the bottom of the text column.
    const int16_t barH = 14;
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", (int)roundf(book.progress * 100.0f));
    const int16_t pctW = (int16_t)(textWidth(FontBody, "100%") + 6);
    Rect barRow{info.x, (int16_t)(card.bottom() - indicatorH - barH - 6), info.width, (int16_t)(barH + 6)};
    progress(f, Rect{barRow.x, (int16_t)(barRow.y + 3), (int16_t)(barRow.width - pctW - kGap), barH},
             (int32_t)(book.progress * 1000.0f));
    text(Rect{(int16_t)(barRow.right() - pctW), barRow.y, pctW, barRow.height}, pct, FontBody, Color::Black,
         TextAlign::Right);
}

}  // namespace

void drawHomeScreen(AppContext& ctx, bool fullRefresh) {
    uint32_t t0 = millis();
    Pass pass;
    Frame& f = pass.frame();
    clear();

    Rect area = appHeader(f);
    {
        // BACK: sleep on the book card, else back to it; LEFT/RIGHT: other recent
        // books on the card, LEFT syncs on the sync row; OK/RIGHT open.
        const int sel = ctx.selectedHomeIndex;
        KeyLabels keys;
        keys.back = tr(sel == 0 ? MsgId::KEY_SLEEP : MsgId::KEY_BACK);
        keys.ok = tr(MsgId::KEY_OPEN);
        keys.left = sel == 0 ? "\xE2\x97\x80" : sel == 3 ? tr(MsgId::KEY_SYNC) : "";
        keys.right = sel == 0 ? "\xE2\x96\xB6" : tr(MsgId::KEY_OPEN);
        area = keyHints(f, area, keys);
    }
    area = insetX(area, kMargin);

    // Breadcrumb.
    takeTop(area, 4);
    text(takeTop(area, (int16_t)(lineHeight(FontSmall) + 6)), tr(MsgId::HOME_SUBHEADER_ALL), FontSmall);
    takeTop(area, 8);

    // Featured card.
    auto& lib = LibraryManager::getInstance();
    std::vector<RecentBookInfo> rBooks = lib.getRecentBooks();
    if (lib.recentBookIndex >= (int)rBooks.size()) lib.recentBookIndex = 0;
    RecentBookInfo curBook;
    if (!rBooks.empty()) {
        curBook = rBooks[lib.recentBookIndex];
    } else {
        curBook.title = tr(MsgId::HOME_EMPTY_TITLE);
        curBook.author = tr(MsgId::HOME_EMPTY_HINT);
        curBook.progress = 0.0f;
    }
    // Portrait: the book card on top, the rows under it. Landscape (the screen
    // turned): the card on the left, the rows on the right; stacked, the rows
    // fell off the bottom of the screen and could not be reached.
    const bool landscape = width() > height();
    if (landscape) {
        const int16_t cardW = (int16_t)(area.width * 56 / 100);
        Rect cardArea{area.x, area.y, cardW, (int16_t)(area.height - 8)};
        drawFeatured(f, cardArea, ctx, curBook, lib.recentBookIndex, (int)rBooks.size());
        const int16_t gap = 18;
        vline((int16_t)(area.x + cardW + gap / 2), area.y, (int16_t)(area.bottom() - 8));
        area = Rect{(int16_t)(area.x + cardW + gap), area.y, (int16_t)(area.width - cardW - gap), area.height};
    } else {
        const int16_t cardH = (int16_t)(230 + lineHeight(FontSmall) + 8);
        drawFeatured(f, takeTop(area, cardH), ctx, curBook, lib.recentBookIndex, (int)rBooks.size());
        takeTop(area, 10);
    }
    hline(area.x, area.right(), area.y);
    takeTop(area, 6);

    // Categories: Catalog, Settings
    fi::ListItem items[2];
    char bookCountBuf[16];
    int totalBooks = lib.getCounts().total;
    snprintf(bookCountBuf, sizeof(bookCountBuf), "%d", totalBooks);

    items[0].label = tr(MsgId::HOME_CAT_CATALOG);
    items[0].value = bookCountBuf;
    items[0].actionValue = 1;

    items[1].label = tr(MsgId::HOME_CAT_SETTINGS);
    items[1].value = tr(MsgId::HOME_CAT_SETTINGS_SUB);
    items[1].actionValue = 2;

    int16_t rowH = (int16_t)((area.height - 20 - 3 * 4) / 3);
    const int16_t minRow = (int16_t)(lineHeight(FontBody) + 20), maxRow = (int16_t)(lineHeight(FontBody) + 36);
    if (rowH < minRow) rowH = minRow;
    if (rowH > maxRow) rowH = maxRow;

    fi::ListProps lp;
    lp.items = items;
    lp.count = 2;
    lp.selectedIndex = (ctx.selectedHomeIndex >= 1 && ctx.selectedHomeIndex <= 2) ? ctx.selectedHomeIndex - 1 : -1;
    lp.action = ActionCategory;
    lp.rowHeight = rowH;
    lp.rowGap = 4;
    lp.sidePadding = 12;
    lp.labelText = style(FontBody);
    lp.valueText = style(FontBody, Color::Black, TextAlign::Right);
    lp.scrollIndicator = false;
    fi::list(f, takeTop(area, (int16_t)(2 * (rowH + 4))), lp);

    takeTop(area, 4);
    hline(area.x, area.right(), area.y);
    takeTop(area, 6);

    // Sync row.
    Rect sync = takeTop(area, rowH);
    const bool syncSel = (ctx.selectedHomeIndex == 3);
    const Color ink = syncSel ? Color::White : Color::Black;
    if (syncSel) fill(sync, Color::Black);
    text(insetX(sync, 12), tr(MsgId::HOME_SYNC_NOW), FontBody, ink);
    icon::sync((int16_t)(sync.right() - 12 - 20), (int16_t)(sync.y + (sync.height - 20) / 2), ink);
    f.hit(sync, ActionSync);
    takeTop(area, 6);
    hline(area.x, area.right(), area.y);

    present(fullRefresh);
    Serial.printf("[UI] drawHomeScreen finished in %lu ms.\n", millis() - t0);
}
