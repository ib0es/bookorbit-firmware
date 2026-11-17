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

constexpr int16_t kArrowWidth = 34;
constexpr int16_t kArrowHeight = 44;

void drawBookArrow(Frame& f, int16_t x, int16_t centerY, bool previous) {
    const Rect hit{x, (int16_t)(centerY - kArrowHeight / 2), kArrowWidth, kArrowHeight};
    text(hit, previous ? "<" : ">", FontTitle, Color::Black, TextAlign::Center);
    f.hit(hit, previous ? HomeHit::PreviousBook : HomeHit::NextBook);
}

// Cover-first card shared by the portrait and landscape layouts.
void drawFeatured(Frame& f, Rect card, const RecentBookInfo& book, int index, int total, bool selected, bool arrows) {
    if (selected)
        selectionFrame(card);
    else
        frameRect(card, Color::Black, 1, kRadius);

    f.hit(card, HomeHit::OpenBook, index);
    Rect in = insetXY(card, 16, 16);
    Rect info = in;
    const int16_t titleHeight = wrappedHeight(book.title.c_str(), FontTitle, info.width, 2);
    const int16_t details = titleHeight + lineHeight(FontSmall) + (total ? 8 : 4) + 8;
    const Rect cover =
            insetXY(takeTop(info, std::max<int>(1, info.height - details)), arrows && total > 1 ? kArrowWidth : 0, 0);
    takeTop(info, 8);

    bool coverDrawn = false;
    // P4: book.coverPath is LibraryManager::recentCover()'s answer, kept until the
    // library changes; an empty one means "no cover": the card is not searched
    // again (about 12 file checks) at every key press.
    const String& covPath = book.coverPath;
    if (covPath.length() > 0) {
        coverDrawn =
                JpegViewer::drawCover(covPath.c_str(), cover.x, cover.y, cover.width, cover.height, COVER_FIT_SCALE);
    }
    if (!coverDrawn) {
        JpegViewer::drawCoverPlaceholder(book.title.c_str(), "", cover.x, cover.y, cover.width, cover.height);
    }

    if (arrows && total > 1) {
        const int16_t centerY = cover.y + cover.height / 2;
        if (index > 0) drawBookArrow(f, in.x, centerY, true);
        if (index + 1 < total) drawBookArrow(f, in.right() - kArrowWidth, centerY, false);
    }

    const int16_t smallH = lineHeight(FontSmall);
    if (total > 0) {
        Rect foot = takeBottom(info, smallH);
        takeBottom(info, 8);
        char pct[16];
        snprintf(pct, sizeof(pct), "%d%%", (int)roundf(book.progress * 100.0f));
        const int16_t pctW = textWidth(FontSmall, pct) + 4;
        text(Rect{(int16_t)(foot.right() - pctW), foot.y, pctW, foot.height}, pct, FontSmall, Color::Black,
             TextAlign::Right);
        progress(f, Rect{foot.x, (int16_t)(foot.y + (foot.height - 6) / 2), (int16_t)(foot.width - pctW - kGap), 6},
                 (int32_t)(book.progress * 1000.0f));
    }
    const TextAlign align = TextAlign::Center;
    const int16_t titleH = wrappedHeight(book.title.c_str(), FontTitle, info.width, 2);
    text(takeTop(info, titleH), book.title.c_str(), FontTitle, Color::Black, align, 2);
    if (total == 0) {
        takeTop(info, 4);
        text(takeTop(info, smallH), tr(MsgId::HOME_EMPTY_HINT), FontSmall, Color::Black, align);
    }
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
    f.hit(Rect{cell.x, cell.y, cell.width, (int16_t)(lr.bottom() - cell.y)}, HomeHit::Tile, (int16_t)t.index);
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
        curBook.progress = 0.0f;
    }

    const Tile tiles[] = {
            {HOME_CATALOG, MsgId::HOME_TILE_CATALOG, icon::menuBook, -1},
            {HOME_SYNC_NOW, MsgId::HOME_TILE_SYNC, icon::syncLarge, -1},
            {HOME_SETTINGS, MsgId::HOME_CAT_SETTINGS, icon::settings, -1},
    };

    // Portrait: three tiles below the book. Landscape: one column beside it.
    const bool landscape = width() > height();
    constexpr int16_t gap = 18;
    constexpr int tileCount = sizeof(tiles) / sizeof(tiles[0]);
    Rect grid;
    int cols;
    if (landscape) {
        const int16_t rowH = (area.height - (tileCount - 1) * gap) / tileCount;
        int16_t columnW = rowH - lineHeight(FontBody) - 8;
        for (const Tile& tile : tiles) columnW = std::max<int16_t>(columnW, textWidth(FontSmall, tr(tile.label)));
        const int16_t cardW = area.width - columnW - 24;
        const int total = rBooks.size();
        const int active = lib.recentBookIndex;
        const int count = total >= 2 ? 2 : 1;
        const int first = count == 2 ? std::min(active, total - 2) : 0;
        const int16_t gutters = total > 1 ? 2 * kArrowWidth : 0;
        const int16_t width = std::min<int>(area.height * 3 / 4, (cardW - gutters - (count - 1) * gap) / count);
        const int16_t startX = area.x + (cardW - count * width - (count - 1) * gap) / 2;
        for (int i = 0; i < count; ++i) {
            const int index = first + i;
            const Rect card{(int16_t)(startX + i * (width + gap)), area.y, width, area.height};
            drawFeatured(f, card, total ? rBooks[index] : curBook, index, total,
                         ctx.selectedHomeIndex == HOME_RECENT_BOOK && index == active, false);
        }
        const int16_t centerY = area.y + area.height / 2;
        if (active > 0) drawBookArrow(f, startX - kArrowWidth, centerY, true);
        if (active + 1 < total) drawBookArrow(f, startX + count * width + (count - 1) * gap, centerY, false);
        grid = Rect{(int16_t)(area.x + cardW + 24), area.y, (int16_t)(area.width - cardW - 24), area.height};
        cols = 1;
    } else {
        const int16_t tileW = (area.width - 2 * 18) / 3;
        const int16_t tileH = tileW + lineHeight(FontBody) + 8;
        const int16_t cardH = area.height - tileH - 22;
        drawFeatured(f, takeTop(area, cardH), curBook, lib.recentBookIndex, (int)rBooks.size(),
                     ctx.selectedHomeIndex == HOME_RECENT_BOOK, true);
        takeTop(area, 22);
        grid = area;
        cols = 3;
    }
    const int16_t cellW = (int16_t)((grid.width - (cols - 1) * gap) / cols);
    const int rows = (tileCount + cols - 1) / cols;
    const int16_t cellH =
            (int16_t)std::min<int>(cellW + lineHeight(FontBody) + 8, (grid.height - (rows - 1) * gap) / rows);
    for (int i = 0; i < tileCount; i++) {
        const int r = i / cols, c = i % cols;
        Rect cell{(int16_t)(grid.x + c * (cellW + gap)), (int16_t)(grid.y + r * (cellH + gap)), cellW, cellH};
        drawTile(f, cell, tiles[i], ctx.selectedHomeIndex == tiles[i].index);
    }

    present(fullRefresh);
    Serial.printf("[UI] drawHomeScreen finished in %lu ms.\n", millis() - t0);
}
