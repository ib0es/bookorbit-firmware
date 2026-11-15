#include <algorithm>
#include "reader_menu_view.h"
#include "../../platform/hal/display.h"
#include "../../ui/font.h"
#include "../../reader/reader.h"
#include "../../reader/font_store.h"
#include "../../app/screens/appearance.h"
#include "../../app/screens/status_bar_menu.h"
#include "../../services/bookmarks.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"

using namespace uikit;

static int s_tocRowsPerPage = 1;
// What the footer took beyond the estimate the last time (the touch hints of a
// touch-only board, a hint that wrapped): kept so the next pass gets it right.
static int16_t s_tocFooterExtra = 0;
int tocRowsPerPage() { return s_tocRowsPerPage; }

namespace {}

void drawTocScreen(AppContext& ctx, bool fullRefresh) {
    uint32_t t0 = millis();
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::READER_TOC_TITLE));

    const auto& chapters = Reader.getChapters();
    const int total = (int)chapters.size();
    if (total == 0) {
        area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), "", "", ""});
        area = insetXY(area, kMargin, 20);
        takeTop(area, area.height / 4);
        text(takeTop(area, (int16_t)(lineHeight(FontBody) * 2)), tr(MsgId::READER_TOC_EMPTY), FontBody, Color::Black,
             TextAlign::Center, 2);
        present(false);
        return;
    }

    // The footer shows "item x/y, page p/q": the page size depends on the
    // row height, so compute it before writing the hint.
    // The hint line may wrap (a long translation): measure it with the widest numbers.
    char probe[160];
    snprintf(probe, sizeof(probe), tr(MsgId::READER_TOC_NAV_HINT_FMT), total, total, total, total);
    const int16_t footerH =
            (int16_t)(wrappedHeight(probe, FontSmall, (int16_t)(width() - 2 * (kMargin + 4)), 2) + 6 + 8);
    const int16_t rowH = rowHeight(true);
    const int16_t step = (int16_t)(rowH + 4);
    const int16_t tabsH = touchOnly() ? 0 : (int16_t)(lineHeight(FontSmall) + 10 + 8);
    const int16_t listH = (int16_t)(area.height - footerH - tabsH - 20 - s_tocFooterExtra);
    int perPage = (listH + 4) / step;
    if (perPage < 1) perPage = 1;
    s_tocRowsPerPage = perPage;
    const int windowStart = (ctx.selectedTocIndex / perPage) * perPage;
    const int windowEnd = min(windowStart + perPage, total);

    char navHints[160];
    snprintf(navHints, sizeof(navHints), tr(MsgId::READER_TOC_NAV_HINT_FMT), ctx.selectedTocIndex + 1, total,
             ctx.selectedTocIndex / perPage + 1, (total + perPage - 1) / perPage);
    // LEFT / RIGHT: a page of chapters back / on.
    area = keyHints(f, area,
                    KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_GO), "\xE2\x97\x80\xE2\x97\x80",
                              "\xE2\x96\xB6\xE2\x96\xB6"},
                    navHints);
    area = insetXY(area, kMargin, 10);
    if ((area.height + 4) / step < perPage) {
        // The rows do not fit under the footer as drawn: draw again with its real height.
        s_tocFooterExtra = (int16_t)(s_tocFooterExtra + listH - area.height);
        drawTocScreen(ctx, fullRefresh);
        return;
    }

    const int currentChapterIdx = Reader.getCurrentChapterIndex();
    const int totalPages = max(1, Reader.getTotalPages());
    for (int i = windowStart; i < windowEnd; i++) {
        Rect row = takeTop(area, rowH);
        takeTop(area, 4);
        const bool selected = (i == ctx.selectedTocIndex);
        const bool current = (i == currentChapterIdx);
        const Color ink = Color::Black;
        if (selected) selectionFrame(row);
        f.hit(row, ActionChapter, (int16_t)i);

        Rect inner = insetXY(row, 16, 8);
        char pageBuf[32];
        snprintf(pageBuf, sizeof(pageBuf), tr(MsgId::READER_TOC_PAGE_FMT), chapters[i].page);
        char progBuf[32];
        snprintf(progBuf, sizeof(progBuf), tr(MsgId::READER_TOC_PROGRESS_FMT),
                 (float)chapters[i].page / (float)totalPages * 100.0f);
        const int16_t rightW = (int16_t)(max(textWidth(FontSmall, pageBuf), textWidth(FontSmall, progBuf)) + 4);
        Rect right{(int16_t)(inner.right() - rightW), inner.y, rightW, inner.height};
        text(takeTop(right, lineHeight(FontBody)), pageBuf, FontSmall, ink, TextAlign::Right);
        text(right, progBuf, FontSmall, ink, TextAlign::Right);

        Rect title{inner.x, inner.y, (int16_t)(inner.width - rightW - kGap), inner.height};
        if (current) {
            text(Rect{title.x, title.y, 16, title.height}, "\xE2\x80\xA2", FontBody, ink);  // bullet
            title.x = (int16_t)(title.x + 18);
            title.width = (int16_t)(title.width - 18);
        }
        text(title, chapters[i].title.c_str(), FontBody, ink, TextAlign::Left, 2);
    }

    present(fullRefresh);
    Serial.printf("[UI] drawTocScreen finished in %lu ms (row %d/%d, %d a page).\n", millis() - t0,
                  ctx.selectedTocIndex + 1, total, perPage);
}

void drawReaderMenuScreen(AppContext& ctx, bool fullRefresh) {
    uint32_t t0 = millis();
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::READER_MENU_TITLE));
    area = keyHints(f, area,
                    KeyLabels{tr(MsgId::KEY_TO_BOOK), tr(MsgId::KEY_SELECT), tr(MsgId::KEY_TO_BOOK),
                              tr(MsgId::KEY_SELECT)});
    area = insetXY(area, kMargin, 8);

    // Book status card.
    {
        const int16_t th = lineHeight(FontTitle), sh = lineHeight(FontSmall);
        Rect st = takeTop(area, (int16_t)(th + 2 * (sh + 2) + 7 + 30));
        frameRect(st, Color::Black, 1, kRadius);
        Rect in = insetXY(st, 16, 8);
        text(takeTop(in, th), Reader.getTitle().c_str(), FontTitle);
        takeTop(in, 2);
        char posBuf[80];
        snprintf(posBuf, sizeof(posBuf), tr(MsgId::READER_MENU_PAGE_FMT), Reader.getCurrentPage(),
                 Reader.getTotalPages(), Reader.getProgress() * 100.0f);
        text(takeTop(in, (int16_t)(sh + 2)), posBuf, FontSmall);
        String chTitle = Reader.getCurrentChapterTitle();
        char chBuf[128];
        if (chTitle.length() > 0)
            snprintf(chBuf, sizeof(chBuf), tr(MsgId::READER_MENU_CHAPTER_FMT), chTitle.c_str());
        else
            snprintf(chBuf, sizeof(chBuf), "%s", tr(MsgId::READER_MENU_START_BOOK));
        text(takeTop(in, (int16_t)(sh + 2)), chBuf, FontSmall);
        takeTop(in, 5);
        progress(f, takeTop(in, 7), (int32_t)(Reader.getProgress() * 1000.0f));
    }
    takeTop(area, 8);

    int chCount = Reader.getChapterCount();
    char chDesc[128];
    if (chCount > 0)
        snprintf(chDesc, sizeof(chDesc), tr(MsgId::READER_MENU_TOC_SUB_FMT), chCount);
    else
        snprintf(chDesc, sizeof(chDesc), "%s", tr(MsgId::READER_MENU_TOC_NONE));
    char fontVal[128];
    snprintf(fontVal, sizeof(fontVal), tr(MsgId::READER_MENU_APPEARANCE_FMT), Reader.fontFamilyName().c_str(),
             BookReader::getFontSizeName(ctx.config.readerFontSize));

    char marksVal[96];
    snprintf(marksVal, sizeof(marksVal), tr(MsgId::READER_MENU_BOOKMARKS_FMT), Bookmarks.count());

    struct Item {
        const char* title;
        const char* value;
        const char* tag;
    };
    const Item items[NUM_READER_MENU_ITEMS] = {
            {tr(MsgId::READER_MENU_ITEM_TOC), chDesc, tr(MsgId::READER_MENU_TAG_CHAPTERS)},
            {tr(MsgId::READER_MENU_ITEM_APPEARANCE), fontVal, tr(MsgId::READER_MENU_TAG_FONT)},
            {tr(MsgId::READER_MENU_ITEM_BOOKMARKS), marksVal, tr(MsgId::READER_MENU_TAG_MARKS)},
            {tr(MsgId::READER_MENU_ITEM_SYNC), tr(MsgId::READER_MENU_SYNC_SUB), tr(MsgId::READER_MENU_TAG_WIFI)},
            {tr(MsgId::READER_MENU_ITEM_RESUME), tr(MsgId::READER_MENU_RESUME_SUB), tr(MsgId::READER_MENU_TAG_READ)},
            {tr(MsgId::READER_MENU_ITEM_CLOSE), tr(MsgId::READER_MENU_CLOSE_SUB), tr(MsgId::READER_MENU_TAG_EXIT)},
    };
    const int n = readerMenuItemCount();
    const int sel = ctx.selectedReaderMenuIndex;
    // A landscape screen: tighter cards, then a page at a time.
    const CardList l = cardList(area, n, sel, rowHeight(true), 2);
    const int16_t cardH = l.cardH, gap = l.gap;
    const int first = l.first, perPage = l.perPage;
    for (int i = first; i < n && i < first + perPage; i++) {
        card(f, takeTop(area, cardH), items[i].title, items[i].value, items[i].tag, i == sel, ActionMenuItem,
             (int16_t)i);
        takeTop(area, gap);
    }

    present(fullRefresh);
    Serial.printf("[UI] drawReaderMenuScreen finished in %lu ms.\n", millis() - t0);
}

static int s_fontWindowStart = 0;
static int s_fontRowsVisible = 1;
int fontMenuRowsVisible() { return s_fontRowsVisible; }

void drawFontMenuScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    const int n = FontEngine::kSizeCount;
    const int sel = ctx.selectedFontIndex < 0 ? 0 : (ctx.selectedFontIndex >= n ? n - 1 : ctx.selectedFontIndex);
    Rect area = topBar(f, tr(MsgId::FONT_MENU_TITLE));
    area = keyHints(f, area,
                    KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_APPLY), tr(MsgId::KEY_BACK), tr(MsgId::KEY_APPLY)});
    area = insetXY(area, kMargin, 8);

    // Rows grow with their font, so not all sizes fit: show a window of
    // consecutive sizes that contains the selection.
    const int pad = 14, gap = 4;
    auto rowH = [&](int i) { return Font.getFontHeight(i) + pad; };
    auto fits = [&](int from, int to) {   // rows from..to (inclusive) fit in the area
        int h = 0;
        for (int i = from; i <= to; i++) h += rowH(i) + (i > from ? gap : 0);
        return h <= area.height;
    };
    int start = s_fontWindowStart;
    if (start > sel) start = sel;
    while (start < sel && !fits(start, sel)) start++;
    int end = start;
    while (end + 1 < n && fits(start, end + 1)) end++;
    // Fill the screen when the window reaches the last size.
    while (start > 0 && fits(start - 1, end)) start--;
    s_fontWindowStart = start;
    s_fontRowsVisible = end - start + 1;

    const int16_t labelW = (int16_t)(textWidth(FontBody, "48 pt \xE2\x9C\x93") + 16);
    int16_t y = area.y;
    for (int i = start; i <= end; i++) {
        const int16_t h = (int16_t)rowH(i);
        Rect row{area.x, y, area.width, h};
        const bool isSel = (i == sel);
        const bool cur = (i == ctx.config.readerFontSize);
        if (isSel) selectionFrame(row);
        char label[16];
        snprintf(label, sizeof(label), cur ? "%s \xE2\x9C\x93" : "%s", BookReader::getFontSizeName(i));  // U+2713
        text(Rect{(int16_t)(row.x + 14), row.y, labelW, row.height}, label, FontBody);
        // The sample in the book font itself, vertically centred (black on a white
        // inset, the selected row's frame staying black around it).
        const int sx = row.x + 14 + labelW;
        const uint16_t px = (uint16_t)FontEngine::sizePx(i);
        const int lh = Reader.sampleLineHeight(px);
        Reader.drawSample(sx, row.y + (h - lh) / 2, row.right() - 8 - sx, lh, tr(MsgId::FONT_MENU_SAMPLE), px);
        f.hit(row, ActionFontSize, (int16_t)i);
        y = (int16_t)(y + h + gap);
    }
    // More sizes above / below: small arrows at the right edge of the gap rows.
    if (start > 0) text(Rect{(int16_t)(area.right() - 24), (int16_t)(area.y - 8), 24, 10}, "\xE2\x96\xB2", FontSmall);
    if (end < n - 1) text(Rect{(int16_t)(area.right() - 24), (int16_t)(y - 2), 24, 12}, "\xE2\x96\xBC", FontSmall);
    present(fullRefresh);
}

// ------------------------------------------------------------- typefaces

static std::vector<FontStore::Family> s_families;

const std::vector<FontStore::Family>& typefaceFamilies(bool rescan, int storage) {
    if (rescan) {
        s_families = storage == 1 ? FontStore::installedFamilies() : FontStore::scanCard();
    }
    return s_families;
}

void drawTypefaceMenuScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    const bool sources = ctx.typefaceStorage == 0;
    const auto& fams = s_families;
    const int n = sources ? 3 : (int)fams.size();
    int& sel = ctx.selectedTypefaceIndex;
    sel = std::max(0, std::min(sel, n - 1));
    const MsgId titleId = sources ? MsgId::TYPEFACE_MENU_TITLE
                                  : (ctx.typefaceStorage == 1 ? MsgId::TYPEFACE_FLASH_TITLE : MsgId::TYPEFACE_SD_TITLE);
    Rect area = topBar(f, tr(titleId));
    const KeyLabels hints{tr(MsgId::KEY_BACK), tr(sources ? MsgId::KEY_SELECT : MsgId::KEY_APPLY),
                          !sources && n ? tr(MsgId::KEY_DELETE) : "",
                          tr(sources ? MsgId::KEY_SELECT : MsgId::KEY_APPLY)};
    // The keys on screen are what the tests (test/sim) check against what the keys do.
    Serial.printf("[UI] drawTypefaceMenuScreen: storage=%d rows=%d selected=%d keys=[%s|%s|%s|%s]\n",
                  ctx.typefaceStorage, n, sel, hints.back, hints.ok, hints.left, hints.right);
    area = keyHints(f, area, hints);
    area = insetXY(area, kMargin, 8);
    const int16_t rowH = rowHeight(true);
    const int perPage = std::max(1, (int)(area.height / (rowH + 6)));
    const int first = (sel / perPage) * perPage;
    if (n > perPage) setListPager(n, perPage);
    for (int i = first; i < n && i < first + perPage; i++) {
        String name, note;
        if (sources) {
            name = i == 0 ? String(Reader.builtinFontName())
                          : String(tr(i == 1 ? MsgId::TYPEFACE_FLASH_TITLE : MsgId::TYPEFACE_SD_TITLE));
            if (i == 0) note = tr(MsgId::TYPEFACE_BUILTIN);
        } else {
            name = fams[i].name;
            char buf[96];
            if (ctx.typefaceStorage == 1)
                snprintf(buf, sizeof(buf), "%s", tr(MsgId::TYPEFACE_INSTALLED));
            else
                snprintf(buf, sizeof(buf), tr(MsgId::TYPEFACE_ON_CARD_FMT), (unsigned)(fams[i].bytes / 1024));
            note = buf;
        }
        const bool current = sources ? i == 0 && Reader.usingBuiltinFont()
                                     : !Reader.usingBuiltinFont() && Reader.fontFamilyName() == name;
        if (current) name += " \xE2\x9C\x93";
        Rect row = takeTop(area, rowH);
        if (!sources && touchUi()) {
            const int16_t dw = (int16_t)std::min<int>(rowH, 96);
            Rect del{(int16_t)(row.right() - dw), row.y, dw, row.height};
            row.width = (int16_t)(row.width - dw - kGap);
            frameRect(del, Color::Black, 1, kRadius);
            text(del, tr(MsgId::KEY_DELETE), FontSmall, Color::Black, TextAlign::Center, 2);
            f.hit(del, ActionTypefaceDelete, (int16_t)i);
        }
        card(f, row, name.c_str(), note.c_str(), nullptr, i == sel, ActionTypeface, (int16_t)i);
        takeTop(area, 6);
    }
    if (!sources && n == 0) {
        text(takeTop(area, (int16_t)(lineHeight(FontSmall) * 2 + 4)), tr(MsgId::TYPEFACE_STORAGE_EMPTY), FontSmall,
             Color::Black, TextAlign::Center, 2);
    }
    present(fullRefresh);
}

// ------------------------------------------------------------- appearance

namespace {
// A row "label ... value". On touch boards a stepping row gets "-" and "+"
// beside its value, so a value can go down by touch too (C5); a tap on the
// rest of the row still selects it (and steps up).
void valueRow(Frame& f, Rect row, const char* label, const char* val, bool isSel, bool stepping, fi::ActionId rowAction,
              int16_t index, int toggleOn = -1) {
    if (isSel) selectionFrame(row);
    const Color ink = Color::Black;
    Rect in = insetX(row, 14);
    if (toggleOn >= 0) {
        // On / off: a switch; a tap anywhere on the row (or a step) flips it.
        f.hit(row, rowAction, index);
        toggle(Rect{(int16_t)(in.right() - 46), row.y, 46, row.height}, toggleOn > 0);
        text(Rect{in.x, in.y, (int16_t)(in.width - 46 - 8), in.height}, label, FontBody, ink);
        return;
    }
    const int16_t vw = (int16_t)(textWidth(FontBody, val) + 4);
    f.hit(row, rowAction, index);
    if (stepping && touchUi()) {
        // Finger-sized: as tall as the row allows, wider than tall.
        const int16_t bw = (int16_t)std::min<int>(row.height, 52);
        Rect plus{(int16_t)(in.right() - bw), (int16_t)(row.y + 4), bw, (int16_t)(row.height - 8)};
        Rect value{(int16_t)(plus.x - 6 - vw), in.y, vw, in.height};
        Rect minus{(int16_t)(value.x - 6 - bw), plus.y, bw, plus.height};
        for (const Rect& b : {minus, plus}) frameRect(b, ink, 1, kRadiusSmall);
        text(minus, "-", FontLarge, ink, TextAlign::Center);
        text(plus, "+", FontLarge, ink, TextAlign::Center);
        text(value, val, FontBody, ink, TextAlign::Center);
        text(Rect{in.x, in.y, (int16_t)(minus.x - in.x - 8), in.height}, label, FontBody, ink);
        f.hit(minus, ActionValueLess, index);
        f.hit(plus, ActionValueMore, index);
    } else {
        text(Rect{(int16_t)(in.right() - vw), in.y, vw, in.height}, val, FontBody, ink, TextAlign::Right);
        text(Rect{in.x, in.y, (int16_t)(in.width - vw - 8), in.height}, label, FontBody, ink);
    }
}
}  // namespace

void drawAppearanceScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    int& sel = ctx.selectedAppearanceIndex;
    const int page = ctx.appearancePage;
    if (sel < 0) sel = 0;
    const int rows = Appearance::count(page);
    if (sel >= rows) sel = rows - 1;
    Rect area = topBar(f, tr(page == 1 ? MsgId::APPEAR_MORE : MsgId::APPEAR_TITLE));
    {
        const int r = Appearance::rowAt(sel, page);
        // OK opens a list (typeface, size) or the "More" page there
        const bool list = r == Appearance::TYPEFACE || r == Appearance::SIZE || r == Appearance::MORE;
        area = keyHints(f, area,
                        KeyLabels{tr(MsgId::KEY_DONE), tr(list ? MsgId::KEY_OPEN : MsgId::KEY_CHANGE), "\xE2\x97\x80",
                                  "\xE2\x96\xB6"});
    }
    area = insetXY(area, kMargin, 2);
    const int16_t rowH = rowHeight(false);
    char val[64];
    // All the rows when they fit (with room for the preview below), else the
    // page of rows the selection is on (a landscape screen).
    const int perPage = std::max(1, (int)(area.height / (rowH + 2)));
    const int first = rows > perPage ? (sel / perPage) * perPage : 0;
    if (rows > perPage) setListPager(rows, perPage);
    for (int i = first; i < rows && i < first + perPage; i++) {
        Rect row = takeTop(area, rowH);
        const int r = Appearance::rowAt(i, page);
        if (r == Appearance::MORE) {   // opens the second page
            RowOpts o;
            o.chevron = true;
            uikit::row(f, row, Appearance::label(r), nullptr, o, i == sel, ActionAppearance, (int16_t)i);
            takeTop(area, 2);
            continue;
        }
        Appearance::value(ctx.config, r, val, sizeof(val));
        // "-" / "+" beside the numbers only; the typeface opens a list, the
        // other rows (alignment, orientation) change with a tap on them.
        const bool stepping = r == Appearance::SIZE || r == Appearance::LINE_SPACING ||
                              r == Appearance::PARAGRAPH_SPACING || r == Appearance::MARGINS;
        valueRow(f, row, Appearance::label(r), val, i == sel, stepping, ActionAppearance, (int16_t)i,
                 Appearance::toggleState(ctx.config, r));
        takeTop(area, 2);
    }
    // The settings as they will look: size, typeface, line spacing, margins.
    takeTop(area, 6);
    if (area.height > 40) {
        hline(0, width(), area.y);
        // Drawn on every pass: the screen was cleared above, so a preview skipped
        // "because nothing changed" would vanish when only the selection moves.
        Reader.drawPreview(area.y + 6, area.height - 8, tr(MsgId::APPEAR_SAMPLE), Appearance::layoutOptions(ctx.config),
                           ctx.config.readerFontSize);
    }
    present(fullRefresh);
}

// ------------------------------------------------------------- bookmarks

// Whether the page on screen has a bookmark: the index, -1 if not.
int bookmarkOnPage() {
    int spine;
    uint32_t from, to;
    return Reader.pageRange(spine, from, to) ? Bookmarks.findIn(spine, from, to) : -1;
}

void drawBookmarksScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    const int n = 1 + Bookmarks.count();
    int& sel = ctx.selectedBookmarkIndex;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    Rect area = topBar(f, tr(MsgId::BOOKMARKS_TITLE));
    const int here = bookmarkOnPage();
    KeyLabels keys{tr(MsgId::KEY_BACK), tr(sel == 0 ? (here >= 0 ? MsgId::KEY_REMOVE : MsgId::KEY_ADD) : MsgId::KEY_GO),
                   sel > 0 ? tr(MsgId::KEY_DELETE) : "",
                   tr(sel == 0 ? (here >= 0 ? MsgId::KEY_REMOVE : MsgId::KEY_ADD) : MsgId::KEY_GO)};
    area = keyHints(f, area, keys);
    area = insetXY(area, kMargin, 10);
    const int16_t rowH = rowHeight(true);
    const int perPage = std::max(1, (int)(area.height / (rowH + kGap)));
    const int first = (sel / perPage) * perPage;
    if (n > perPage) setListPager(n, perPage);
    char title[160];
    for (int i = first; i < n && i < first + perPage; i++) {
        if (i == 0) {
            card(f, takeTop(area, rowH), tr(here >= 0 ? MsgId::BOOKMARKS_REMOVE : MsgId::BOOKMARKS_ADD),
                 tr(MsgId::BOOKMARKS_ADD_SUB), nullptr, sel == 0, ActionBookmark, 0);
        } else {
            const Bookmark& b = Bookmarks.items()[i - 1];
            if (b.placed()) {
                snprintf(title, sizeof(title), tr(MsgId::BOOKMARKS_ITEM_FMT), (int)(b.percent * 100.0f + 0.5f),
                         b.chapter.c_str());
            } else {   // from the server, its place not worked out yet: no percent
                snprintf(title, sizeof(title), "%s", b.chapter.c_str());
            }
            Rect r = takeTop(area, rowH);
            if (touchUi()) {   // a "delete" target beside the card (C1): no LEFT key here
                const int16_t dw = (int16_t)std::min<int>(rowH, 96);
                Rect del{(int16_t)(r.right() - dw), r.y, dw, r.height};
                r.width = (int16_t)(r.width - dw - kGap);
                frameRect(del, Color::Black, 1, kRadius);
                text(del, tr(MsgId::KEY_DELETE), FontSmall, Color::Black, TextAlign::Center, 2);
                f.hit(del, ActionBookmarkDelete, (int16_t)i);
            }
            card(f, r, title, b.snippet.c_str(), nullptr, sel == i, ActionBookmark, (int16_t)i);
        }
        takeTop(area, kGap);
    }
    if (n == 1) {
        takeTop(area, 20);
        text(takeTop(area, (int16_t)(lineHeight(FontBody) * 2 + 4)), tr(MsgId::BOOKMARKS_NONE), FontBody, Color::Black,
             TextAlign::Center, 2);
    }
    present(fullRefresh);
}
