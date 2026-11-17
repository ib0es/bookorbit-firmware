#include <cmath>
#include "library_view.h"
#include "../../services/library_manager.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"
#include "../../core/book_formats.h"
#include <string>
#include <vector>

using namespace uikit;

namespace {

enum LibraryAction : fi::ActionId { ActionOpenItem = 1, ActionLaunchWeb = 2 };

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

void drawRow(Frame& f, Rect r, const BookItem& item, bool selected, int index, bool searchResult, bool offline) {
    if (selected) selectionFrame(r);
    f.hit(r, ActionOpenItem, (int16_t)index);

    Rect inner = insetXY(r, 14, 4);
    // Search results use the catalog icons, independently of download status.
    const int16_t iconS = searchResult && !item.isDirectory ? 40 : 30;
    const bool savedOnline = searchResult && !offline && !item.isDirectory && item.isDownloaded;
    Rect iconBox{inner.x, (int16_t)(r.y + (r.height - iconS) / 2), iconS, iconS};
    char statusBuf[32];
    const char* status = statusBuf;
    statusBuf[0] = '\0';

    if (item.isDirectory) {
        icon::folderLarge(iconBox, Color::Black);
        if (item.title.startsWith("..")) {
            status = tr(MsgId::LIB_BADGE_UP);
        } else if (item.subItemCount > 0) {
            snprintf(statusBuf, sizeof(statusBuf), "%d", item.subItemCount);
        } else {
            status = tr(MsgId::LIB_BADGE_FOLDER);
        }
    } else {
        if (searchResult) {
            const auto drawIcon = offline ? icon::cloudOff : (savedOnline ? icon::cloudDone : icon::cloudDownload);
            drawIcon(iconBox, Color::Black);
        } else if (item.isDownloaded)
            icon::bookOutline(iconBox, Color::Black);
        else
            icon::cloudLarge(iconBox, Color::Black);
        // On the card: how far it is read; only in the cloud: say so (no position
        // is asked for until it is downloaded).
        if (!item.isDownloaded) {
            status = tr(MsgId::HOME_CAT_CLOUD);
        } else if (item.progress >= LibraryManager::kCompletedAt) {
            status = tr(MsgId::LIB_BADGE_COMPLETED);
        } else if (item.progress > 0.0f) {
            // Any position at all, rounded as on the home screen's card.
            snprintf(statusBuf, sizeof(statusBuf), "%d%%", (int)roundf(item.progress * 100.0f));
        }
    }
    // A format the reader cannot open yet (format list).
    if (item.formatRow >= 0 && !BookFormats::readable((BookFormats::Kind)item.activeKind()))
        status = tr(MsgId::LIB_BADGE_UNSUPPORTED);

    // A book in several formats: their labels ("EPUB · FB2"), shortened to
    // "EPUB +3" when they would take too much of the row.
    std::string formats;
    if (!item.isDirectory && item.formatRow < 0 && item.formats.size() > 1) {
        std::vector<BookFormats::Kind> kinds;
        for (const auto& fm : item.formats) kinds.push_back((BookFormats::Kind)fm.kind);
        formats = BookFormats::labels(kinds);
        if (textWidth(FontSmall, formats.c_str()) > inner.width / 3) {
            const size_t sep = formats.find(' ');
            char shortBuf[24];
            snprintf(shortBuf, sizeof(shortBuf), "%s +%d", formats.substr(0, sep).c_str(),
                     (int)item.formats.size() - 1);
            formats = shortBuf;
        }
    }

    String displayTitle = item.title;
    if (!item.isDirectory && item.seriesIndex > 0) {
        char position[40];
        snprintf(position, sizeof(position), "%.15g", item.seriesIndex);
        displayTitle = "#" + String(position) + " " + item.title;
    }

    const int16_t lh = lineHeight(FontBody), sh = lineHeight(FontSmall);
    Rect text0{(int16_t)(iconBox.right() + 14), inner.y, (int16_t)(inner.right() - iconBox.right() - 14), inner.height};
    // The status on the right of the first line (one-line rows: of the row).
    const int16_t statusW = *status ? (int16_t)(textWidth(FontSmall, status) + 4) : 0;
    const bool twoLines = r.height >= lh + sh;

    if (item.isDirectory || !twoLines) {
        if (statusW)
            text(Rect{(int16_t)(text0.right() - statusW), r.y, statusW, r.height}, status, FontSmall, Color::Black,
                 TextAlign::Right);
        text(Rect{text0.x, r.y, (int16_t)(text0.width - statusW - (statusW ? kGap : 0)), r.height},
             displayTitle.c_str(), FontBody);
        return;
    }
    // U1: two lines, so books of one series ("Saga. Book 1", "Saga. Book 2")
    // are told apart: a title that does not fit wraps onto the second line;
    // one that fits has its author there.
    const int16_t titleW = (int16_t)(text0.width - statusW - (statusW ? kGap : 0));
    const bool wraps = wrappedHeight(displayTitle.c_str(), FontBody, titleW, 2) > lh + 2;
    const bool author = !wraps && (item.author.length() > 0 || !formats.empty());
    const int16_t blockH = (wraps || author) ? (int16_t)(2 * lh - 4) : lh;
    const int16_t top = (int16_t)(r.y + (r.height - blockH) / 2);
    if (statusW)
        text(Rect{(int16_t)(text0.right() - statusW), top, statusW, lh}, status, FontSmall, Color::Black,
             TextAlign::Right);
    if (wraps) {
        text(Rect{text0.x, top, titleW, blockH}, displayTitle.c_str(), FontBody, Color::Black, TextAlign::Left, 2);
        return;
    }
    text(Rect{text0.x, top, titleW, lh}, displayTitle.c_str(), FontBody);
    if (!author) return;
    Rect second{text0.x, (int16_t)(top + lh - 2), text0.width, sh};
    if (!formats.empty()) {
        const int16_t fw = (int16_t)(textWidth(FontSmall, formats.c_str()) + 4);
        text(Rect{(int16_t)(second.right() - fw), second.y, fw, sh}, formats.c_str(), FontSmall, Color::Black,
             TextAlign::Right);
        second.width = (int16_t)(second.width - fw - kGap);
    }
    if (item.formatRow < 0) text(second, item.author.c_str(), FontSmall);
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
    if (lib.currentFilter == FILTER_DOWNLOADED)
        filterName = tr(MsgId::LIB_FILTER_DOWNLOADED);
    else if (lib.currentFilter == FILTER_CLOUD)
        filterName = tr(MsgId::LIB_FILTER_CLOUD);
    else if (lib.currentFilter == FILTER_COMPLETED)
        filterName = tr(MsgId::LIB_FILTER_COMPLETED);
    else if (lib.currentFilter == FILTER_FOLDERS)
        filterName = tr(MsgId::LIB_FILTER_FOLDERS);

    // Virtual lists: "<name> (<entries>)"; the ".." row is not counted.
    const int entries = total > 0 && lib.rowAt(0).title.startsWith("..") ? total - 1 : total;
    const String& folder = lib.currentFolder;
    if (folder.equals("@formats") && lib.formatBookItem()) {
        snprintf(titleBuf, sizeof(titleBuf), tr(MsgId::LIB_FORMATS_HEADER_FMT), lib.formatBookItem()->title.c_str());
    } else if (lib.currentFilter == FILTER_OFFLINE) {
        if (total == 0)
            snprintf(titleBuf, sizeof(titleBuf), "%s", tr(MsgId::LIB_TITLE_OFFLINE));
        else
            snprintf(titleBuf, sizeof(titleBuf), "%s (%d/%d)", tr(MsgId::LIB_TITLE_OFFLINE), lib.selectedBookIndex + 1,
                     total);
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
        if (total == 0)
            snprintf(titleBuf, sizeof(titleBuf), "%s", filterName);
        else
            snprintf(titleBuf, sizeof(titleBuf), "%s (%d/%d)", filterName, lib.selectedBookIndex + 1, total);
    } else {
        int lastSlash = lib.currentFolder.lastIndexOf('/');
        String folderName = (lastSlash >= 0) ? lib.currentFolder.substring(lastSlash + 1) : lib.currentFolder;
        char name[64];
        snprintf(name, sizeof(name), tr(MsgId::LIB_FOLDER_HEADER_FMT), folderName.c_str());
        if (total == 0)
            snprintf(titleBuf, sizeof(titleBuf), "%s", name);
        else
            snprintf(titleBuf, sizeof(titleBuf), "%s (%d/%d)", name, lib.selectedBookIndex + 1, total);
    }

    clipUtf8(titleBuf);  // snprintf may have cut a long name inside a character
    Rect area = topBar(f, titleBuf);

    const bool curIsDir = total > 0 && lib.rowAt(lib.selectedBookIndex).isDirectory;
    const bool curDownloaded = total > 0 && lib.rowAt(lib.selectedBookIndex).isDownloaded;
    if (curIsDir) {
        area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_OPEN), "", tr(MsgId::KEY_OPEN)});
    } else if (folder.equals("@formats")) {  // format rows have no book menu
        area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_READ), "", tr(MsgId::KEY_READ)});
    } else if (lib.currentFilter == FILTER_OFFLINE) {
        if (total == 0) {
            area = keyHints(f, area, KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_WEB), "", tr(MsgId::KEY_WEB)});
        } else {
            area = keyHints(f, area,
                            KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_READ), tr(MsgId::KEY_MENU),
                                      tr(MsgId::KEY_WEB)});
        }
    } else {
        (void)curDownloaded;
        area = keyHints(f, area,
                        KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_READ), tr(MsgId::KEY_MENU), tr(MsgId::KEY_READ)});
    }
    area = insetXY(area, kMargin, 10);

    if (total == 0) {
        takeTop(area, lib.currentFilter == FILTER_OFFLINE ? 10 : (area.height / 4));
        const MsgId msgs[3] = {
                lib.currentFilter == FILTER_OFFLINE ? MsgId::LIB_EMPTY_OFFLINE_1 : MsgId::LIB_EMPTY_MSG_1,
                lib.currentFilter == FILTER_OFFLINE ? MsgId::LIB_EMPTY_OFFLINE_2 : MsgId::LIB_EMPTY_MSG_2,
                lib.currentFilter == FILTER_OFFLINE ? MsgId::LIB_EMPTY_OFFLINE_3 : MsgId::LIB_EMPTY_MSG_3,
        };
        for (MsgId m : msgs) {
            const int16_t h = wrappedHeight(tr(m), FontBody, area.width, 3);
            text(takeTop(area, h), tr(m), FontBody, Color::Black, TextAlign::Left, 3);
            takeTop(area, 10);
        }
        if (lib.currentFilter == FILTER_OFFLINE) {
            takeTop(area, 12);
            const int16_t cardH = (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 24);
            Rect cardR = takeTop(area, cardH);
            card(f, cardR, tr(MsgId::LIB_EMPTY_OFFLINE_BTN), tr(MsgId::LIB_EMPTY_OFFLINE_SUB), tr(MsgId::KEY_WEB), true,
                 ActionLaunchWeb, 0);
        }
    } else {
        const int16_t rowH = (int16_t)(2 * lineHeight(FontBody) + 8);  // two lines (U1)
        const int16_t gap = 6;
        const int16_t step = (int16_t)(rowH + gap);
        int perPage = (area.height + gap) / step;
        if (perPage < 1) perPage = 1;
        s_rowsPerPage = perPage;
        const int pageStart = (lib.selectedBookIndex / perPage) * perPage;
        // Room on the right for the scroll track when there is more than a page.
        const bool paged = total > perPage;
        Rect listArea = area;
        if (paged) listArea.width = (int16_t)(listArea.width - 14);
        const Rect track{(int16_t)(area.right() - 6), area.y, 8, (int16_t)(perPage * step - gap)};
        for (int i = pageStart; i < total && i < pageStart + perPage; i++) {
            Rect row = takeTop(listArea, rowH);
            takeTop(listArea, gap);
            const bool searchResult = folder.equalsIgnoreCase("@search");
            drawRow(f, row, lib.rowAt(i), i == lib.selectedBookIndex, i, searchResult,
                    searchResult && lib.rowMasterIndex(i) < 0);
        }
        // A search with no match: say so under the ".." row.
        if (entries == 0 && (lib.currentFilter == FILTER_SEARCH || folder.equalsIgnoreCase("@search"))) {
            takeTop(listArea, 20);
            text(takeTop(listArea, (int16_t)(lineHeight(FontBody) + 8)), tr(MsgId::CATALOG_SEARCH_NOT_FOUND), FontBody,
                 Color::Black, TextAlign::Center);
        }
        if (paged) scrollbar(track, pageStart, perPage, total);
    }

    present(fullRefresh);
    Serial.printf("[UI] drawLibraryScreen finished in %lu ms.\n", millis() - t0);
    if (total > 0) {  // what is selected, for the logs (and the walkthrough tests)
        const BookItem& sel = lib.rowAt(lib.selectedBookIndex);
        Serial.printf("[UI] Library row %d/%d: '%s' %s\n", lib.selectedBookIndex + 1, total, sel.title.c_str(),
                      sel.isDirectory    ? "folder"
                      : sel.isDownloaded ? "on card"
                                         : "cloud");
    }
}
