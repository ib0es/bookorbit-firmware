#include <cmath>
#include "library_view.h"
#include "../../services/library_manager.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"
#include "../../net/book_formats.h"
#include <string>
#include <vector>

using namespace uikit;

namespace {

enum LibraryAction : fi::ActionId { ActionOpenItem = 1 };

// Drops a UTF-8 sequence left incomplete at the end of a buffer.
void clipUtf8(char* s) {
    size_t n = strlen(s);
    size_t i = n;
    while (i > 0 && (((uint8_t)s[i - 1]) & 0xC0) == 0x80) i--;   // continuation bytes
    if (i == 0) return;
    const uint8_t lead = (uint8_t)s[i - 1];
    const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    if (n - (i - 1) < need) s[i - 1] = '\0';
}

void drawRow(Frame& f, Rect r, const BookItem& item, bool selected, int index) {
    const Color ink = selected ? Color::White : Color::Black;
    if (selected) fill(r, Color::Black);
    else frameRect(r, Color::Black);
    f.hit(r, ActionOpenItem, (int16_t)index);

    Rect inner = insetX(r, 10);
    const int16_t iconY = (int16_t)(r.y + r.height / 2 - 5);
    char badgeBuf[32];
    const char* badge = badgeBuf;
    bool badgeDark = true;

    if (item.isDirectory) {
        icon::folder(inner.x, iconY, ink);
        if (item.title.startsWith("..")) {
            badge = tr(MsgId::LIB_BADGE_UP);
        } else if (item.subItemCount > 0) {
            snprintf(badgeBuf, sizeof(badgeBuf), "%d", item.subItemCount);
        } else {
            badge = tr(MsgId::LIB_BADGE_FOLDER);
        }
    } else {
        // Icon: where the book is. Badge: how far it is read (or where it is).
        if (item.isDownloaded) icon::check(inner.x, iconY, ink);
        else icon::cloud(inner.x, iconY, ink);
        // Filled pill = on the card, outlined pill = only in the cloud; a cloud
        // book with progress also says so ("15% · cloud").
        char pct[12];
        const char* read = nullptr;
        if (item.progress >= 0.90f) read = tr(MsgId::LIB_BADGE_COMPLETED);
        else if (item.progress > 0.01f) { snprintf(pct, sizeof(pct), "%d%%", (int)roundf(item.progress * 100.0f)); read = pct; }
        if (item.isDownloaded) {
            snprintf(badgeBuf, sizeof(badgeBuf), "%s", read ? read : tr(MsgId::HOME_CAT_DOWNLOADED));   // pct is local
        } else {
            badgeDark = false;
            if (read) snprintf(badgeBuf, sizeof(badgeBuf), tr(MsgId::LIB_BADGE_IN_CLOUD_FMT), read);
            else badge = tr(MsgId::HOME_CAT_CLOUD);
        }
    }

    // A format the reader cannot open yet (format list).
    if (item.formatRow >= 0 && !BookFormats::readable((BookFormats::Kind)item.activeKind())) {
        badge = tr(MsgId::LIB_BADGE_UNSUPPORTED);
        badgeDark = false;
    }

    const int16_t badgeW = pillWidth(badge);
    const int16_t badgeH = (int16_t)(lineHeight(FontSmall) + 6);
    pill(Rect{(int16_t)(inner.right() - badgeW), (int16_t)(r.y + (r.height - badgeH) / 2), badgeW, badgeH}, badge,
         badgeDark, selected);
    int16_t pillsW = badgeW;

    // A book in several formats: their labels ("EPUB · FB2"), shortened to
    // "EPUB +3" when they would take too much of the row.
    if (!item.isDirectory && item.formatRow < 0 && item.formats.size() > 1) {
        std::vector<BookFormats::Kind> kinds;
        for (const auto& fm : item.formats) kinds.push_back((BookFormats::Kind)fm.kind);
        std::string labels = BookFormats::labels(kinds);
        if (pillWidth(labels.c_str()) > inner.width / 3) {
            const size_t sep = labels.find(' ');
            char shortBuf[24];
            snprintf(shortBuf, sizeof(shortBuf), "%s +%d", labels.substr(0, sep).c_str(), (int)item.formats.size() - 1);
            labels = shortBuf;
        }
        const int16_t w = pillWidth(labels.c_str());
        pill(Rect{(int16_t)(inner.right() - badgeW - kGap - w), (int16_t)(r.y + (r.height - badgeH) / 2), w, badgeH},
             labels.c_str(), false, selected);
        pillsW = (int16_t)(pillsW + kGap + w);
    }

    String displayTitle = item.title;
    if (!item.isDirectory && item.seriesIndex > 0) {
        displayTitle = "#" + String(item.seriesIndex) + " " + item.title;
    }

    Rect title{(int16_t)(inner.x + 24), r.y, (int16_t)(inner.width - 24 - pillsW - kGap), r.height};
    text(title, displayTitle.c_str(), FontBody, ink);
}

}  // namespace

static int s_rowsPerPage = 1;
int libraryRowsPerPage() { return s_rowsPerPage; }

void drawLibraryScreen(AppContext& ctx, bool fullRefresh) {
    (void)ctx;
    uint32_t t0 = millis();
    Pass pass;
    Frame& f = pass.frame();
    clear();

    auto& lib = LibraryManager::getInstance();
    const int total = lib.rowCount();

    char titleBuf[96];
    const char* filterName = tr(MsgId::LIB_FILTER_ALL);
    if (lib.currentFilter == FILTER_DOWNLOADED) filterName = tr(MsgId::LIB_FILTER_DOWNLOADED);
    else if (lib.currentFilter == FILTER_CLOUD) filterName = tr(MsgId::LIB_FILTER_CLOUD);
    else if (lib.currentFilter == FILTER_COMPLETED) filterName = tr(MsgId::LIB_FILTER_COMPLETED);
    else if (lib.currentFilter == FILTER_FOLDERS) filterName = tr(MsgId::LIB_FILTER_FOLDERS);

    // Virtual lists: "<name> (<entries>)"; the ".." row is not counted.
    const int entries = total > 0 && lib.rowAt(0).title.startsWith("..") ? total - 1 : total;
    const String& folder = lib.currentFolder;
    if (folder.equals("@formats") && lib.formatsBook >= 0 && lib.formatsBook < (int)lib.masterBooks.size()) {
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_FORMATS_HEADER_FMT), lib.masterBooks[lib.formatsBook].title.c_str());
    } else if (folder.equalsIgnoreCase("@libraries")) {
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_LIBRARIES_HEADER_FMT), entries);
    } else if (folder.startsWith("@library:")) {
        String l = folder.substring(9);
        if (l == "__none__") l = tr(MsgId::CATALOG_UNKNOWN_LIBRARY);
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_LIBRARY_HEADER_FMT), l.c_str(), entries);
    } else if (folder.equalsIgnoreCase("@authors")) {
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_AUTHORS_HEADER_FMT), entries);
    } else if (folder.startsWith("@author:")) {
        String a = folder.substring(8);
        if (a.length() == 0) a = tr(MsgId::CATALOG_UNKNOWN_AUTHOR);
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_AUTHOR_HEADER_FMT), a.c_str(), entries);
    } else if (folder.equalsIgnoreCase("@series")) {
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_SERIES_ALL_HEADER_FMT), entries);
    } else if (folder.startsWith("@series:")) {
        String sr = folder.substring(8);
        if (sr == "__none__") sr = tr(MsgId::CATALOG_STANDALONE);
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_SERIES_HEADER_FMT), sr.c_str(), entries);
    } else if (lib.currentFilter == FILTER_SEARCH || folder.equalsIgnoreCase("@search")) {
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_SEARCH_HEADER_FMT), lib.searchQuery.c_str(), entries);
    } else if (lib.currentFolder.equalsIgnoreCase("/books") || lib.currentFolder.equals("/")) {
        if (total == 0) snprintf(titleBuf, sizeof(titleBuf), "%s", filterName);
        else snprintf(titleBuf, sizeof(titleBuf), "%s (%d/%d)", filterName, lib.selectedBookIndex + 1, total);
    } else {
        int lastSlash = lib.currentFolder.lastIndexOf('/');
        String folderName = (lastSlash >= 0) ? lib.currentFolder.substring(lastSlash + 1) : lib.currentFolder;
        char name[64];
        snprintf(name, sizeof(name), tr(MsgId::LIB_FOLDER_HEADER_FMT), folderName.c_str());
        if (total == 0) snprintf(titleBuf, sizeof(titleBuf), "%s", name);
        else snprintf(titleBuf, sizeof(titleBuf), "%s (%d/%d)", name, lib.selectedBookIndex + 1, total);
    }

    clipUtf8(titleBuf);   // snprintf may have cut a long name inside a character
    Rect area = topBar(f, titleBuf);

    const bool curIsDir = total > 0 && lib.rowAt(lib.selectedBookIndex).isDirectory;
    const bool curDownloaded = total > 0 && lib.rowAt(lib.selectedBookIndex).isDownloaded;
    if (curIsDir) {
        area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_OPEN), "", tr(MsgId::KEY_OPEN)});
    } else if (folder.equals("@formats")) {   // format rows have no book menu
        area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_READ), "", tr(MsgId::KEY_READ)});
    } else {
        (void)curDownloaded;
        area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_READ), tr(MsgId::KEY_MENU), tr(MsgId::KEY_READ)});
    }
    area = insetXY(area, kMargin, 10);

    if (total == 0) {
        takeTop(area, area.height / 4);
        const MsgId msgs[3] = { MsgId::LIB_EMPTY_MSG_1, MsgId::LIB_EMPTY_MSG_2, MsgId::LIB_EMPTY_MSG_3 };
        for (MsgId m : msgs) {
            const int16_t h = wrappedHeight(tr(m), FontBody, area.width, 3);
            text(takeTop(area, h), tr(m), FontBody, Color::Black, TextAlign::Left, 3);
            takeTop(area, 12);
        }
    } else {
        const int16_t rowH = (int16_t)(lineHeight(FontBody) + 20);
        const int16_t step = (int16_t)(rowH + 5);
        int perPage = (area.height + 5) / step;
        if (perPage < 1) perPage = 1;
        s_rowsPerPage = perPage;
        const int pageStart = (lib.selectedBookIndex / perPage) * perPage;
        for (int i = pageStart; i < total && i < pageStart + perPage; i++) {
            Rect row = takeTop(area, rowH);
            takeTop(area, 5);
            drawRow(f, row, lib.rowAt(i), i == lib.selectedBookIndex, i);
        }
        // A search with no match: say so under the ".." row.
        if (entries == 0 && (lib.currentFilter == FILTER_SEARCH || folder.equalsIgnoreCase("@search"))) {
            takeTop(area, 20);
            text(takeTop(area, (int16_t)(lineHeight(FontBody) + 8)), tr(MsgId::CATALOG_SEARCH_NOT_FOUND), FontBody,
                 Color::Black, TextAlign::Center);
        }
        // Page indicator when the list has more than one page.
        const int pages = (total + perPage - 1) / perPage;
        if (pages > 1) {
            const int16_t trackX = (int16_t)(width() - 6);
            Rect track{trackX, (int16_t)(lineHeight(FontTitle) + 28), 3,
                       (int16_t)(perPage * step - 5)};
            vline(trackX + 1, track.y, track.bottom(), Color::Black, 1);
            const int16_t thumbH = (int16_t)(track.height / pages);
            fill(Rect{trackX, (int16_t)(track.y + (pageStart / perPage) * thumbH), 3, thumbH}, Color::Black);
        }
    }

    present(fullRefresh);
    Serial.printf("[UI] drawLibraryScreen finished in %lu ms.\n", millis() - t0);
}
