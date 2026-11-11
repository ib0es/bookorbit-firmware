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
int tocRowsPerPage() { return s_tocRowsPerPage; }

namespace {
}

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
    const int16_t footerH = (int16_t)(lineHeight(FontSmall) + 4 + 14);
    const int16_t rowH = (int16_t)(lineHeight(FontBody) + lineHeight(FontSmall) + 18);
    const int16_t step = (int16_t)(rowH + 6);
    const int16_t listH = (int16_t)(area.height - footerH - 20);
    int perPage = (listH + 6) / step;
    if (perPage < 1) perPage = 1;
    s_tocRowsPerPage = perPage;
    const int windowStart = (ctx.selectedTocIndex / perPage) * perPage;
    const int windowEnd = min(windowStart + perPage, total);

    char navHints[160];
    snprintf(navHints, sizeof(navHints), tr(MsgId::READER_TOC_NAV_HINT_FMT), ctx.selectedTocIndex + 1, total,
             ctx.selectedTocIndex / perPage + 1, (total + perPage - 1) / perPage);
    // LEFT / RIGHT: a page of chapters back / on.
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_GO), "\xE2\x97\x80\xE2\x97\x80", "\xE2\x96\xB6\xE2\x96\xB6"}, navHints);
    area = insetXY(area, kMargin, 10);

    const int currentChapterIdx = Reader.getCurrentChapterIndex();
    const int totalPages = max(1, Reader.getTotalPages());
    for (int i = windowStart; i < windowEnd; i++) {
        Rect row = takeTop(area, rowH);
        takeTop(area, 6);
        const bool selected = (i == ctx.selectedTocIndex);
        const bool current = (i == currentChapterIdx);
        const Color ink = selected ? Color::White : Color::Black;
        if (selected) fill(row, Color::Black);
        else frameRect(row, Color::Black);
        f.hit(row, ActionChapter, (int16_t)i);

        Rect inner = insetXY(row, 12, 8);
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
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_TO_BOOK), tr(MsgId::KEY_SELECT), tr(MsgId::KEY_TO_BOOK), tr(MsgId::KEY_SELECT)});
    area = insetXY(area, kMargin, 8);

    // Book status card.
    {
        const int16_t th = lineHeight(FontTitle), sh = lineHeight(FontSmall);
        Rect st = takeTop(area, (int16_t)(th + 2 * (sh + 2) + 7 + 30));
        frameRect(st, Color::Black);
        Rect in = insetXY(st, 12, 8);
        text(takeTop(in, th), Reader.getTitle().c_str(), FontTitle);
        takeTop(in, 2);
        char posBuf[80];
        snprintf(posBuf, sizeof(posBuf), tr(MsgId::READER_MENU_PAGE_FMT), Reader.getCurrentPage(),
                 Reader.getTotalPages(), Reader.getProgress() * 100.0f);
        text(takeTop(in, (int16_t)(sh + 2)), posBuf, FontSmall);
        String chTitle = Reader.getCurrentChapterTitle();
        char chBuf[128];
        if (chTitle.length() > 0) snprintf(chBuf, sizeof(chBuf), tr(MsgId::READER_MENU_CHAPTER_FMT), chTitle.c_str());
        else snprintf(chBuf, sizeof(chBuf), "%s", tr(MsgId::READER_MENU_START_BOOK));
        text(takeTop(in, (int16_t)(sh + 2)), chBuf, FontSmall);
        takeTop(in, 5);
        progress(f, takeTop(in, 7), (int32_t)(Reader.getProgress() * 1000.0f));
    }
    takeTop(area, 8);

    int chCount = Reader.getChapterCount();
    char chDesc[128];
    if (chCount > 0) snprintf(chDesc, sizeof(chDesc), tr(MsgId::READER_MENU_TOC_SUB_FMT), chCount);
    else snprintf(chDesc, sizeof(chDesc), "%s", tr(MsgId::READER_MENU_TOC_NONE));
    char fontVal[128];
    snprintf(fontVal, sizeof(fontVal), tr(MsgId::READER_MENU_APPEARANCE_FMT), Reader.fontFamilyName().c_str(),
             BookReader::getFontSizeName(ctx.config.readerFontSize));

    char marksVal[96];
    snprintf(marksVal, sizeof(marksVal), tr(MsgId::READER_MENU_BOOKMARKS_FMT), Bookmarks.count());

    struct Item { const char* title; const char* value; const char* tag; };
    const Item items[NUM_READER_MENU_ITEMS] = {
        { tr(MsgId::READER_MENU_ITEM_TOC), chDesc, tr(MsgId::READER_MENU_TAG_CHAPTERS) },
        { tr(MsgId::READER_MENU_ITEM_APPEARANCE), fontVal, tr(MsgId::READER_MENU_TAG_FONT) },
        { tr(MsgId::READER_MENU_ITEM_BOOKMARKS), marksVal, tr(MsgId::READER_MENU_TAG_MARKS) },
        { tr(MsgId::READER_MENU_ITEM_SYNC), tr(MsgId::READER_MENU_SYNC_SUB), tr(MsgId::READER_MENU_TAG_WIFI) },
        { tr(MsgId::READER_MENU_ITEM_RESUME), tr(MsgId::READER_MENU_RESUME_SUB), tr(MsgId::READER_MENU_TAG_READ) },
        { tr(MsgId::READER_MENU_ITEM_CLOSE), tr(MsgId::READER_MENU_CLOSE_SUB), tr(MsgId::READER_MENU_TAG_EXIT) },
    };
    const int n = NUM_READER_MENU_ITEMS;
    const int sel = ctx.selectedReaderMenuIndex;
    // A landscape screen: tighter cards, then a page at a time.
    const CardList l = cardList(area, n, sel, (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 22));
    const int16_t cardH = l.cardH, gap = l.gap;
    const int first = l.first, perPage = l.perPage;
    for (int i = first; i < n && i < first + perPage; i++) {
        card(f, takeTop(area, cardH), items[i].title, items[i].value, items[i].tag, i == sel, ActionMenuItem, (int16_t)i);
        takeTop(area, gap);
    }

    // Live preview of the book font (drawn with the reader's own font,
    // so it shows exactly what the pages will look like).
    if (area.height > 60) {
        Rect box = area;
        frameRect(box, Color::Black);
        const int16_t bandH = (int16_t)(lineHeight(FontSmall) + 8);
        Rect band = takeTop(box, bandH);
        fill(band, Color::Black);
        char previewTitle[64];
        snprintf(previewTitle, sizeof(previewTitle), tr(MsgId::READER_MENU_PREVIEW_TITLE_FMT),
                 BookReader::getFontSizeName(ctx.config.readerFontSize));
        text(insetX(band, 8), previewTitle, FontSmall, Color::White);
        Reader.drawSample(box.x + 8, box.y + 6, box.width - 16, box.height - 10, tr(MsgId::READER_MENU_SAMPLE_TEXT),
                          (uint16_t)FontEngine::sizePx(ctx.config.readerFontSize));
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
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_APPLY), tr(MsgId::KEY_BACK), tr(MsgId::KEY_APPLY)});
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
        if (isSel) fill(row, Color::Black, 4);
        else frameRect(row, Color::Black, 1, 4);
        char label[16];
        snprintf(label, sizeof(label), cur ? "%s \xE2\x9C\x93" : "%s", BookReader::getFontSizeName(i));  // U+2713
        text(Rect{(int16_t)(row.x + 10), row.y, labelW, row.height}, label, FontBody, isSel ? Color::White : Color::Black);
        // The sample in the book font itself, vertically centred (black on a white
        // inset, the selected row's frame staying black around it).
        const int sx = row.x + 10 + labelW;
        const uint16_t px = (uint16_t)FontEngine::sizePx(i);
        const int lh = Reader.sampleLineHeight(px);
        if (isSel) fill(Rect{(int16_t)(sx - 4), (int16_t)(row.y + 3), (int16_t)(row.right() - sx), (int16_t)(h - 6)}, Color::White, 3);
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
static String s_installed;

const std::vector<FontStore::Family>& typefaceFamilies(bool rescan) {
    if (rescan) {
        s_families = FontStore::scanCard();
        s_installed = FontStore::installedName();
    }
    return s_families;
}

void drawTypefaceMenuScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    const auto& fams = s_families;
    const int n = 1 + (int)fams.size();
    int& sel = ctx.selectedTypefaceIndex;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    Rect area = topBar(f, tr(MsgId::TYPEFACE_MENU_TITLE));
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_APPLY), tr(MsgId::KEY_BACK), tr(MsgId::KEY_APPLY)});
    area = insetXY(area, kMargin, 8);
    const String current = Reader.fontFamilyName();
    const int16_t rowH = (int16_t)(lineHeight(FontBody) + lineHeight(FontSmall) + 18);
    const int perPage = std::max(1, (int)((area.height - lineHeight(FontSmall) * 2) / (rowH + 6)));
    const int first = (sel / perPage) * perPage;
    if (n > perPage) setListPager(n, perPage);
    for (int i = first; i < n && i < first + perPage; i++) {
        const bool builtin = i == 0;
        const String name = builtin ? String(Reader.builtinFontName()) : fams[i - 1].name;
        char note[96];
        if (builtin) snprintf(note, sizeof(note), "%s", tr(MsgId::TYPEFACE_BUILTIN));
        else if (fams[i - 1].name == s_installed) snprintf(note, sizeof(note), "%s", tr(MsgId::TYPEFACE_INSTALLED));
        else snprintf(note, sizeof(note), tr(MsgId::TYPEFACE_ON_CARD_FMT), (unsigned)(fams[i - 1].bytes / 1024));
        const bool cur = builtin ? Reader.usingBuiltinFont() : !Reader.usingBuiltinFont() && current == name;
        char title[96];
        snprintf(title, sizeof(title), cur ? "%s \xE2\x9C\x93" : "%s", name.c_str());   // U+2713
        card(f, takeTop(area, rowH), title, note, nullptr, i == sel, ActionTypeface, (int16_t)i);
        takeTop(area, 6);
    }
    if (fams.empty()) {
        takeTop(area, 10);
        text(takeTop(area, (int16_t)(lineHeight(FontSmall) * 2 + 4)), tr(MsgId::TYPEFACE_CARD_HINT), FontSmall,
             Color::Black, TextAlign::Center, 2);
    }
    present(fullRefresh);
}

// ------------------------------------------------------------- appearance

namespace {
// A row "label ... value". On touch boards a stepping row gets "-" and "+"
// beside its value, so a value can go down by touch too (C5); a tap on the
// rest of the row still selects it (and steps up).
void valueRow(Frame& f, Rect row, const char* label, const char* val, bool isSel, bool stepping,
              fi::ActionId rowAction, int16_t index) {
    if (isSel) fill(row, Color::Black, 4);
    const Color ink = isSel ? Color::White : Color::Black;
    Rect in = insetX(row, 10);
    const int16_t vw = (int16_t)(textWidth(FontBody, val) + 4);
    f.hit(row, rowAction, index);
    if (stepping && touchUi()) {
        const int16_t bw = (int16_t)std::min<int>(row.height + 8, 52);
        Rect plus{(int16_t)(in.right() - bw), (int16_t)(row.y + 3), bw, (int16_t)(row.height - 6)};
        Rect value{(int16_t)(plus.x - 8 - vw), in.y, vw, in.height};
        Rect minus{(int16_t)(value.x - 8 - bw), plus.y, bw, plus.height};
        for (const Rect& b : { minus, plus }) frameRect(b, ink, 1, 4);
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
    if (!isSel) hline((int16_t)(row.x + 6), (int16_t)(row.right() - 6), (int16_t)(row.bottom() - 1));
}
}  // namespace

void drawAppearanceScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    int& sel = ctx.selectedAppearanceIndex;
    if (sel < 0) sel = 0;
    if (sel >= Appearance::COUNT) sel = Appearance::COUNT - 1;
    Rect area = topBar(f, tr(MsgId::APPEAR_TITLE));
    {
        const bool list = sel == Appearance::TYPEFACE || sel == Appearance::SIZE;   // OK opens a list there
        area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_DONE), tr(list ? MsgId::KEY_OPEN : MsgId::KEY_CHANGE),
                                           "\xE2\x97\x80", "\xE2\x96\xB6"});
    }
    area = insetXY(area, kMargin, 6);
    const int16_t rowH = (int16_t)(lineHeight(FontBody) + 12);
    char val[64];
    // All the rows when they fit (with room for the preview below), else the
    // page of rows the selection is on (a landscape screen).
    const int perPage = std::max(1, (int)(area.height / (rowH + 2)));
    const int first = Appearance::COUNT > perPage ? (sel / perPage) * perPage : 0;
    if (Appearance::COUNT > perPage) setListPager(Appearance::COUNT, perPage);
    for (int i = first; i < Appearance::COUNT && i < first + perPage; i++) {
        Rect row = takeTop(area, rowH);
        Appearance::value(ctx.config, i, val, sizeof(val));
        // The typeface and status bar rows open a list; the others step.
        const bool stepping = i != Appearance::TYPEFACE && i != Appearance::STATUS_BAR;
        valueRow(f, row, Appearance::label(i), val, i == sel, stepping, ActionAppearance, (int16_t)i);
        takeTop(area, 2);
    }
    // The settings as they will look: size, typeface, line spacing, margins.
    takeTop(area, 6);
    if (area.height > 40) {
        hline(0, width(), area.y);
        Reader.drawPreview(area.y + 6, area.height - 8, tr(MsgId::APPEAR_SAMPLE), Appearance::layoutOptions(ctx.config),
                           ctx.config.readerFontSize);
    }
    present(fullRefresh);
}

// ------------------------------------------------------------- status bar

void drawStatusBarScreen(AppContext& ctx, bool fullRefresh) {
    Pass pass;
    Frame& f = pass.frame();
    clear();
    int& sel = ctx.selectedStatusBarIndex;
    if (sel < 0) sel = 0;
    if (sel >= StatusBarMenu::COUNT) sel = StatusBarMenu::COUNT - 1;
    Rect area = topBar(f, tr(MsgId::SB_TITLE));
    area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_CHANGE), "\xE2\x97\x80", "\xE2\x96\xB6"});
    area = insetXY(area, kMargin, 6);
    const int16_t rowH = (int16_t)(lineHeight(FontBody) + 12);
    char val[64];
    for (int i = 0; i < StatusBarMenu::COUNT; i++) {
        Rect row = takeTop(area, rowH);
        StatusBarMenu::value(ctx.config, i, val, sizeof(val));
        valueRow(f, row, StatusBarMenu::label(i), val, i == sel, /*stepping=*/true, ActionStatusBar, (int16_t)i);
        takeTop(area, 2);
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
                   sel > 0 ? tr(MsgId::KEY_DELETE) : "", tr(sel == 0 ? (here >= 0 ? MsgId::KEY_REMOVE : MsgId::KEY_ADD) : MsgId::KEY_GO)};
    area = keyHints(f, area, keys);
    area = insetXY(area, kMargin, 10);
    const int16_t rowH = (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 22);
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
                snprintf(title, sizeof(title), tr(MsgId::BOOKMARKS_ITEM_FMT), (int)(b.percent * 100.0f + 0.5f), b.chapter.c_str());
            } else {   // from the server, its place not worked out yet: no percent
                snprintf(title, sizeof(title), "%s", b.chapter.c_str());
            }
            Rect r = takeTop(area, rowH);
            if (touchUi()) {   // a "delete" target beside the card (C1): no LEFT key here
                const int16_t dw = (int16_t)std::min<int>(rowH, 96);
                Rect del{(int16_t)(r.right() - dw), r.y, dw, r.height};
                r.width = (int16_t)(r.width - dw - kGap);
                frameRect(del, Color::Black, 1, 4);
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
