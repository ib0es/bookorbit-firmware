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

enum HomeAction : fi::ActionId { ActionContinue = 1, ActionCategory };

// Featured "continue reading" card: cover on the left; on the right what it is
// (and which of the recent books), title, author, and at the foot the pages
// read and the progress line.
void drawFeatured(Frame& f, Rect card, AppContext& ctx, const RecentBookInfo& book, int index, int total) {
    const bool selected = (ctx.selectedHomeIndex == 0);
    if (selected)
        selectionFrame(card);
    else
        frameRect(card, Color::Black, 1, kRadius);
    f.hit(card, ActionContinue);

    Rect in = insetXY(card, 16, 16);
    // At most 40 % of the card: in landscape the card is narrow and tall, and the
    // text column beside the cover would have no room (the cover is cropped to fit).
    const int16_t coverW = (int16_t)std::min<int>(in.height * 2 / 3, in.width * 2 / 5);
    Rect cover{in.x, in.y, coverW, in.height};

    bool coverDrawn = false;
    // P4: book.coverPath is LibraryManager::recentCover()'s answer, kept until the
    // library changes; an empty one means "no cover": the card is not searched
    // again (about 12 file checks) at every key press.
    const String& covPath = book.coverPath;
    if (covPath.length() > 0) {
        coverDrawn =
                JpegViewer::drawCover(covPath.c_str(), cover.x, cover.y, cover.width, cover.height, COVER_FIT_CROP);
    }
    if (!coverDrawn) {
        JpegViewer::drawCoverPlaceholder(book.title.c_str(), book.author.c_str(), cover.x, cover.y, cover.width,
                                         cover.height);
    }

    Rect info{(int16_t)(cover.right() + 18), in.y, (int16_t)(in.right() - cover.right() - 18), in.height};
    const int16_t smallH = lineHeight(FontSmall);
    Rect top = takeTop(info, smallH);
    if (total > 1) {
        // Which of the recent books (LEFT / RIGHT step through them).
        char car[24];
        snprintf(car, sizeof(car), "%d/%d", index + 1, total);
        const int16_t cw = (int16_t)(textWidth(FontSmall, car) + 4);
        text(Rect{(int16_t)(top.right() - cw), top.y, cw, top.height}, car, FontSmall, Color::Black, TextAlign::Right);
        top.width = (int16_t)(top.width - cw - kGap);
    }
    text(top, tr(total > 0 ? MsgId::HOME_CONTINUE : MsgId::HOME_READING_NOW), FontSmall);

    takeTop(info, 8);
    const int16_t titleH = wrappedHeight(book.title.c_str(), FontTitle, info.width, 3);
    text(takeTop(info, titleH), book.title.c_str(), FontTitle, Color::Black, TextAlign::Left, 3);
    if (book.author.length() > 0) {
        takeTop(info, 4);
        const int16_t authorH = wrappedHeight(book.author.c_str(), FontSmall, info.width, 2);
        text(takeTop(info, authorH), book.author.c_str(), FontSmall, Color::Black, TextAlign::Left, 2);
    }

    if (total == 0) return;
    // Foot: "Read p / n" and the percentage, the progress line under them.
    Rect bar = takeBottom(info, 6);
    takeBottom(info, 6);
    Rect foot = takeBottom(info, smallH);
    char pct[16];
    snprintf(pct, sizeof(pct), "%d%%", (int)roundf(book.progress * 100.0f));
    const int16_t pctW = (int16_t)(textWidth(FontSmall, pct) + 4);
    text(Rect{(int16_t)(foot.right() - pctW), foot.y, pctW, foot.height}, pct, FontSmall, Color::Black,
         TextAlign::Right);
    if (book.totalPages > 1) {
        char pages[48];
        snprintf(pages, sizeof(pages), tr(MsgId::HOME_PAGES_FMT), book.page, book.totalPages);
        text(Rect{foot.x, foot.y, (int16_t)(foot.width - pctW - kGap), foot.height}, pages, FontSmall);
    }
    progress(f, bar, (int32_t)(book.progress * 1000.0f));
}

struct Tile {
    int index;   // home index
    MsgId label;
    void (*draw)(Rect, Color);
    int count;   // shown in the corner; < 0: none
};

// A tile: a rounded square with the icon, the label under it.
void drawTile(Frame& f, Rect cell, const Tile& t, bool selected) {
    const int16_t lb = lineHeight(FontBody);
    const int16_t side = (int16_t)std::min<int>(cell.width, cell.height - lb - 8);
    Rect box{(int16_t)(cell.x + (cell.width - side) / 2), cell.y, side, side};
    if (selected)
        selectionFrame(box);
    else
        frameRect(box, Color::Black, 1, kRadius);
    const int16_t is = (int16_t)(side * 46 / 100);
    t.draw(Rect{(int16_t)(box.x + (side - is) / 2), (int16_t)(box.y + (side - is) / 2), is, is}, Color::Black);
    if (t.count >= 0) {
        char n[12];
        snprintf(n, sizeof(n), "%d", t.count);
        text(Rect{(int16_t)(box.x + 8), (int16_t)(box.y + 6), (int16_t)(side - 16), lineHeight(FontSmall)}, n,
             FontSmall, Color::Black, TextAlign::Right);
    }
    const char* label = tr(t.label);
    Rect lr{(int16_t)(cell.x - 6), (int16_t)(box.bottom() + 6), (int16_t)(cell.width + 12), lb};
    // A long label (Russian, Spanish) in the small font rather than cut.
    const fi::FontId font = textWidth(FontBody, label) <= lr.width ? FontBody : FontSmall;
    text(lr, label, font, Color::Black, TextAlign::Center);
    f.hit(Rect{cell.x, cell.y, cell.width, (int16_t)(lr.bottom() - cell.y)}, ActionCategory, (int16_t)t.index);
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
        // books on the card, LEFT goes back to card elsewhere; OK/RIGHT open.
        const int sel = ctx.selectedHomeIndex;
        KeyLabels keys;
        keys.back = tr(sel == 0 ? MsgId::KEY_SLEEP : MsgId::KEY_BACK);
        keys.ok = tr(MsgId::KEY_OPEN);
        keys.left = sel == 0 ? "\xE2\x97\x80" : "";
        keys.right = sel == 0 ? "\xE2\x96\xB6" : tr(MsgId::KEY_OPEN);
        area = keyHints(f, area, keys);
    }
    area = insetXY(area, kMargin + 6, 6);

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

    const Tile tiles[4] = {
            {1, MsgId::HOME_TILE_CATALOG, icon::cloudOutline, lib.getCounts().total},
            {2, MsgId::HOME_TILE_OFFLINE, icon::menuBook, lib.offlineCount()},
            {3, MsgId::HOME_TILE_SYNC, icon::syncLarge, -1},
            {4, MsgId::HOME_CAT_SETTINGS, icon::settings, -1},
    };

    // Portrait: the book card on top, three tiles a row under it. Landscape (the
    // screen turned): the card on the left, the tiles two a row on the right.
    const bool landscape = width() > height();
    Rect grid;
    int cols;
    if (landscape) {
        const int16_t cardW = (int16_t)(area.width * 55 / 100);
        drawFeatured(f, Rect{area.x, area.y, cardW, area.height}, ctx, curBook, lib.recentBookIndex,
                     (int)rBooks.size());
        grid = Rect{(int16_t)(area.x + cardW + 24), area.y, (int16_t)(area.width - cardW - 24), area.height};
        cols = 2;
    } else {
        const int16_t cardH = (int16_t)std::min<int>(area.height * 42 / 100, 300);
        drawFeatured(f, takeTop(area, cardH), ctx, curBook, lib.recentBookIndex, (int)rBooks.size());
        takeTop(area, 22);
        grid = area;
        cols = 3;
    }
    const int16_t gap = 18;
    const int16_t cellW = (int16_t)((grid.width - (cols - 1) * gap) / cols);
    const int rows = (4 + cols - 1) / cols;
    const int16_t cellH =
            (int16_t)std::min<int>(cellW + lineHeight(FontBody) + 8, (grid.height - (rows - 1) * gap) / rows);
    for (int i = 0; i < 4; i++) {
        const int r = i / cols, c = i % cols;
        Rect cell{(int16_t)(grid.x + c * (cellW + gap)), (int16_t)(grid.y + r * (cellH + gap)), cellW, cellH};
        drawTile(f, cell, tiles[i], ctx.selectedHomeIndex == tiles[i].index);
    }

    present(fullRefresh);
    Serial.printf("[UI] drawHomeScreen finished in %lu ms.\n", millis() - t0);
}
