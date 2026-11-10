#include "screens.h"
#include <algorithm>
#include "../../hal/storage.h"
#include "../../services/library_manager.h"
#include "../../services/sync_service.h"
#include "../../ui/modal.h"
#include "../../ui/views/home_view.h"
#include "../../ui/views/catalog_view.h"
#include "../../ui/views/library_view.h"
#include "../../ui/views/book_menu_view.h"
#include "../../ui/i18n.h"
#include "../../ui/kit/ui_kit.h"
#include "../../net/book_formats.h"
#include "../../ui/text_utils.h"

namespace {

// The selected row; `masterIdxOut` gets its index in masterBooks (-1 for a folder row).
BookItem* selectedBook(LibraryManager& lib, int* masterIdxOut = nullptr) {
    if (lib.selectedBookIndex < 0 || lib.selectedBookIndex >= lib.rowCount()) return nullptr;
    if (masterIdxOut) *masterIdxOut = lib.rowMasterIndex(lib.selectedBookIndex);
    return &lib.rowAt(lib.selectedBookIndex);
}

void goBack(AppContext& ctx);

// "FB2 is not supported yet": the reader opens EPUB and TXT for now.
void showUnsupported(const BookItem& book) {
    // A row of the format list is titled by its format: name the book.
    auto& lib = LibraryManager::getInstance();
    const String& title = (book.formatRow >= 0 && lib.formatsBook >= 0 && lib.formatsBook < (int)lib.masterBooks.size())
                              ? lib.masterBooks[lib.formatsBook].title : book.title;
    Serial.printf("[Library] '%s': %s cannot be opened yet\n", title.c_str(),
                  BookFormats::label((BookFormats::Kind)book.activeKind()));
    char line[64];
    snprintf(line, sizeof(line), tr(MsgId::LIB_FORMAT_UNSUPPORTED_FMT),
             BookFormats::label((BookFormats::Kind)book.activeKind()));
    Modal.showMessage(tr(MsgId::LIB_FORMAT_UNSUPPORTED_TITLE), utf8Prefix(title, 64), line,
                      tr(MsgId::LIB_FORMAT_UNSUPPORTED_L2), "", 2200, false, tr(MsgId::COMMON_BACK));
}

// Downloads the active format of a book if needed, then opens it.
void openBookFormat(AppContext& ctx, BookItem& book) {
    if (!BookFormats::readable((BookFormats::Kind)book.activeKind()) && !book.formats.empty()) {
        drawLibraryScreen(ctx);
        showUnsupported(book);
        return;
    }
    if (!book.isDownloaded || !Storage.exists(book.filePath.c_str())) {
        if (!SyncService::downloadBook(ctx, book, STATE_LIBRARY)) {
            drawLibraryScreen(ctx);
            return;
        }
    }
    ReaderScreen::openBook(ctx, book.filePath, book.title, book.author, book.coverPath,
                           book.progress, book.page, book.xpath, book.docId, book.fileId);
}

void openSelected(AppContext& ctx) {
    auto& lib = LibraryManager::getInstance();
    int masterIdx = -1;
    BookItem* book = selectedBook(lib, &masterIdx);
    if (!book) return;

    // A row of the format list: that format of the book becomes the active one.
    if (book->formatRow >= 0 && lib.formatsBook >= 0 && lib.formatsBook < (int)lib.masterBooks.size()) {
        BookItem& master = lib.masterBooks[lib.formatsBook];
        if (!BookFormats::readable((BookFormats::Kind)book->activeKind())) {   // the book keeps its format
            showUnsupported(*book);
            return;
        }
        LibraryManager::selectFormat(master, book->formatRow);
        lib.refreshProgressOf(master);
        openBookFormat(ctx, master);
        return;
    }
    if (book->isDirectory && book->filePath == "@formats-up") {
        goBack(ctx);
        return;
    }
    // A book in several formats opens in the one read last; never read here,
    // the formats are listed to choose from.
    if (!book->isDirectory && book->formats.size() > 1 && masterIdx >= 0) {
        const int last = LibraryManager::lastReadFormat(*book);
        if (last < 0) {
            lib.openFormats(masterIdx);
            drawLibraryScreen(ctx, uikit::periodicFullRefresh());
            return;
        }
        LibraryManager::selectFormat(*book, last);
        lib.refreshProgressOf(*book);
    }

    if (book->isDirectory) {
        if (book->filePath == "@catalog") {
            ctx.currentState = STATE_CATALOG;
            drawCatalogScreen(ctx, false);
            return;
        }
        if (book->filePath == "@libraries") {
            lib.currentFolder = "@libraries";
            lib.selectedBookIndex = lib.savedLibraryIndex;
            lib.scanLocalBooks();
            drawLibraryScreen(ctx, uikit::periodicFullRefresh());
            return;
        }
        if (book->filePath == "@authors") {
            lib.currentFolder = "@authors";
            lib.selectedBookIndex = lib.savedAuthorIndex;
            lib.scanLocalBooks();
            drawLibraryScreen(ctx, uikit::periodicFullRefresh());
            return;
        }
        if (book->filePath == "@series") {
            lib.currentFolder = "@series";
            lib.selectedBookIndex = lib.savedSeriesIndex;
            lib.scanLocalBooks();
            drawLibraryScreen(ctx, uikit::periodicFullRefresh());
            return;
        }
        if (book->filePath.startsWith("@library:")) {
            lib.savedLibraryIndex = lib.selectedBookIndex;
            lib.currentFolder = book->filePath;
            lib.selectedBookIndex = 0;
            lib.scanLocalBooks();
            drawLibraryScreen(ctx, uikit::periodicFullRefresh());
            return;
        }
        if (book->filePath.startsWith("@author:")) {
            lib.savedAuthorIndex = lib.selectedBookIndex;
            lib.currentFolder = book->filePath;
            lib.selectedBookIndex = 0;
            lib.scanLocalBooks();
            drawLibraryScreen(ctx, uikit::periodicFullRefresh());
            return;
        }
        if (book->filePath.startsWith("@series:")) {
            lib.savedSeriesIndex = lib.selectedBookIndex;
            lib.currentFolder = book->filePath;
            lib.selectedBookIndex = 0;
            lib.scanLocalBooks();
            drawLibraryScreen(ctx, uikit::periodicFullRefresh());
            return;
        }
        lib.currentFolder = book->filePath;
        lib.selectedBookIndex = 0;
        lib.scanLocalBooks();
        drawLibraryScreen(ctx, uikit::periodicFullRefresh());
        return;
    }

    openBookFormat(ctx, *book);
}

// Runs after the user confirmed deletion. `masterIdx`/`path` identify the book
// when the dialog was opened (-1: a file outside the master list).
void deleteBookFile(int masterIdx, const String& path) {
    auto& lib = LibraryManager::getInstance();
    BookItem* book = nullptr;
    if (masterIdx >= 0 && masterIdx < (int)lib.masterBooks.size() && lib.masterBooks[masterIdx].filePath == path) {
        book = &lib.masterBooks[masterIdx];
    } else {
        for (auto& r : lib.folderRows) if (!r.isDirectory && r.filePath == path) { book = &r; break; }
    }
    if (!book) return;
    // Only the active format goes; the book stays while another format is on
    // the card or the server has it.
    if (book->activeFormat >= 0 && book->activeFormat < (int)book->formats.size()) book->formats[book->activeFormat].onCard = false;
    bool otherOnCard = false;
    for (const auto& f : book->formats) otherOnCard = otherOnCard || f.onCard;
    const bool keepInCatalog = book->inCatalog() || otherOnCard;

    Serial.printf("[Library] Deleting downloaded book file: %s\n", path.c_str());
    // Covers go too (unless another format stays): a book that stays in the
    // catalog gets its cover again when it is downloaded (SyncService::downloadBook).
    LibraryManager::deleteBookArtifacts(*book, /*removeCover=*/!otherOnCard);

    if (keepInCatalog) {
        book->isDownloaded = false;   // the master entry itself
        if (otherOnCard) LibraryManager::chooseFormat(*book);
        lib.updateLastBookAfterDeletion(path);   // off the home carousel too
        lib.rebuildFilteredIndices();
        Modal.showMessage(tr(MsgId::LIB_DELETE_SUCCESS_TITLE), tr(MsgId::LIB_DELETE_SUCCESS_L1), tr(MsgId::LIB_DELETE_SUCCESS_L2),
                          tr(MsgId::LIB_DELETE_SUCCESS_L3), "", 1600);
    } else {
        if (masterIdx >= 0 && book == &lib.masterBooks[masterIdx]) lib.masterBooks.erase(lib.masterBooks.begin() + masterIdx);
        lib.updateLastBookAfterDeletion(path);
        lib.rebuildFilteredIndices();   // also clamps the selection
        Modal.showMessage(tr(MsgId::LIB_DELETE_SUCCESS_TITLE), tr(MsgId::LIB_DELETE_SUCCESS_L1), tr(MsgId::LIB_DELETE_SUCCESS_L2),
                          "", "", 1600);
    }
}

void askDeleteSelected() {
    auto& lib = LibraryManager::getInstance();
    int masterIdx = -1;
    BookItem* book = selectedBook(lib, &masterIdx);
    if (!book || book->isDirectory) return;

    if (book->isDownloaded && Storage.exists(book->filePath.c_str())) {
        const char* noteLine = book->inCatalog()
            ? tr(MsgId::LIB_DELETE_LINE3)
            : tr(MsgId::LIB_DELETE_LINE2);
        const String path = book->filePath;
        Modal.showConfirm(tr(MsgId::LIB_DELETE_TITLE), utf8Prefix(book->title, 64),
                          tr(MsgId::LIB_DELETE_LINE1), noteLine,
                          tr(MsgId::LIB_DELETE_FOOTER),
                          [masterIdx, path]() { deleteBookFile(masterIdx, path); }, nullptr,
                          tr(MsgId::LIB_DELETE_BTN));
    } else {
        Modal.showMessage(tr(MsgId::LIB_FILTER_CLOUD), utf8Prefix(book->title, 64), tr(MsgId::LIB_EMPTY_MSG_1),
                          tr(MsgId::LIB_HINT_DOWNLOAD_BOOK), "", 1800, false, tr(MsgId::COMMON_BACK));
    }
}

void goBack(AppContext& ctx) {
    auto& lib = LibraryManager::getInstance();
    if (lib.currentFolder.equals("@formats")) {
        lib.closeFormats();
        drawLibraryScreen(ctx, uikit::periodicFullRefresh());
    } else if (lib.currentFolder.startsWith("@library:")) {
        lib.currentFolder = "@libraries";
        lib.selectedBookIndex = lib.savedLibraryIndex;
        lib.scanLocalBooks();
        drawLibraryScreen(ctx, uikit::periodicFullRefresh());
    } else if (lib.currentFolder.startsWith("@author:")) {
        lib.currentFolder = "@authors";
        lib.selectedBookIndex = lib.savedAuthorIndex;
        lib.scanLocalBooks();
        drawLibraryScreen(ctx, uikit::periodicFullRefresh());
    } else if (lib.currentFolder.startsWith("@series:")) {
        lib.currentFolder = "@series";
        lib.selectedBookIndex = lib.savedSeriesIndex;
        lib.scanLocalBooks();
        drawLibraryScreen(ctx, uikit::periodicFullRefresh());
    } else if (lib.currentFolder.equalsIgnoreCase("@libraries") ||
               lib.currentFolder.equalsIgnoreCase("@authors") ||
               lib.currentFolder.equalsIgnoreCase("@series") ||
               lib.currentFolder.equalsIgnoreCase("@search") ||
               lib.currentFilter == FILTER_SEARCH) {
        ctx.currentState = STATE_CATALOG;
        drawCatalogScreen(ctx, false);
    } else if (!lib.currentFolder.equalsIgnoreCase("/books") && !lib.currentFolder.equals("/")) {
        int lastSlash = lib.currentFolder.lastIndexOf('/');
        lib.currentFolder = (lastSlash > 0) ? lib.currentFolder.substring(0, lastSlash) : String("/books");
        lib.selectedBookIndex = 0;
        lib.scanLocalBooks();
        drawLibraryScreen(ctx, uikit::periodicFullRefresh());
    } else {
        ctx.currentState = STATE_CATALOG;
        drawCatalogScreen(ctx, false);
    }
}

}  // namespace

namespace LibraryScreen {

void handleEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    auto& lib = LibraryManager::getInstance();
    const int count = lib.rowCount();

    // Held down: a row at a time, then a page at a time after a few repeats.
    const int step = event.repeat >= 4 ? std::max(1, libraryRowsPerPage()) : 1;
    switch (event.id) {
        case BTN_DOWN:
            if (count > 0) {
                const int at = lib.selectedBookIndex;
                // A repeat stops at the end of the list instead of wrapping around.
                lib.selectedBookIndex = event.repeat ? std::min(at + step, count - 1) : (at + 1) % count;
                if (lib.selectedBookIndex != at) drawLibraryScreen(ctx);
            }
            break;
        case BTN_UP:
            if (count > 0) {
                const int at = lib.selectedBookIndex;
                lib.selectedBookIndex = event.repeat ? std::max(at - step, 0) : (at - 1 + count) % count;
                if (lib.selectedBookIndex != at) drawLibraryScreen(ctx);
            }
            break;
        case BTN_CONFIRM:
#if defined(FREEINK_DEVICE_PICCO) && FREEINK_DEVICE_PICCO
            // Picco has no LEFT key: OK on a book opens its menu (Read first),
            // the same as a tap. Folders still open directly.
            if (bookMenuBook()) { openBookMenu(ctx); break; }
#endif
            openSelected(ctx);
            break;
        case BTN_RIGHT:
            openSelected(ctx);
            break;
        case BTN_LEFT:
            openBookMenu(ctx);
            break;
        case BTN_BACK:
            goBack(ctx);
            break;
        default:
            break;
    }
}

bool openFormatsOfSelected(AppContext& ctx) {
    auto& lib = LibraryManager::getInstance();
    int masterIdx = -1;
    BookItem* book = selectedBook(lib, &masterIdx);
    if (!book || book->isDirectory || masterIdx < 0 || book->formats.size() < 2) return false;
    lib.openFormats(masterIdx);
    ctx.currentState = STATE_LIBRARY;
    drawLibraryScreen(ctx, uikit::periodicFullRefresh());
    return true;
}

void openBookMenu(AppContext& ctx) {
    if (!bookMenuBook()) return;  // folders have no menu
    ctx.selectedBookMenuIndex = 0;
    ctx.currentState = STATE_BOOK_MENU;
    drawBookMenuScreen(ctx, false);
}

void handleBookMenuEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    BookItem* book = bookMenuBook();
    if (!book) {
        ctx.currentState = STATE_LIBRARY;
        drawLibraryScreen(ctx);
        return;
    }
    BookMenuAction items[BOOK_MENU_MAX];
    const int n = bookMenuItems(*book, items);
    int& sel = ctx.selectedBookMenuIndex;
    if (sel >= n) sel = n - 1;

    switch (event.id) {
        case BTN_DOWN:
            sel = (sel + 1) % n;
            drawBookMenuScreen(ctx, false);
            return;
        case BTN_UP:
            sel = (sel - 1 + n) % n;
            drawBookMenuScreen(ctx, false);
            return;
        case BTN_BACK:
        case BTN_LEFT:
            ctx.currentState = STATE_LIBRARY;
            drawLibraryScreen(ctx);
            return;
        case BTN_CONFIRM:
        case BTN_RIGHT:
            break;
        default:
            return;
    }

    // Every action leaves the menu; the library is the screen underneath
    // (dialogs redraw it when they close, a download returns to it on failure).
    ctx.currentState = STATE_LIBRARY;
    switch (items[sel]) {
        case BOOK_MENU_READ:
        case BOOK_MENU_DOWNLOAD:
            openSelected(ctx);
            break;
        case BOOK_MENU_SYNC:
            // Back to the menu afterwards: the result message closes onto it.
            ctx.currentState = STATE_BOOK_MENU;
        {
            const auto r = SyncService::syncBookItem(ctx, *book);
            drawBookMenuScreen(ctx, uikit::periodicFullRefresh());  // over the status screen
            SyncService::showBookSyncResult(r, *book);
        }
            break;
        case BOOK_MENU_FORMATS:
            openFormatsOfSelected(ctx);
            break;
        case BOOK_MENU_DELETE:
            drawLibraryScreen(ctx);
            askDeleteSelected();
            break;
        case BOOK_MENU_BACK:
            drawLibraryScreen(ctx);
            break;
    }
}

}  // namespace LibraryScreen
