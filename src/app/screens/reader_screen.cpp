#include "screens.h"
#include "../../ui/font.h"
#include "../../hal/mem_stats.h"
#include "../../reader/reader.h"
#include "../../hal/storage.h"
#include "../../services/library_manager.h"
#include "../../services/progress_store.h"
#include "../../services/progress_sync.h"
#include "../../services/sync_service.h"
#include "../../ui/modal.h"
#include "../../ui/views/home_view.h"
#include "../../ui/views/reader_menu_view.h"
#include "../../ui/i18n.h"

namespace {

constexpr int TOC_ITEMS_PER_PAGE = 8;

// The menu only changes the font setting; the book is re-paginated once,
// when the menu is left (not on every step through the sizes).
void commitFontSize(AppContext& ctx) {
    if (Reader.getFontSize() == ctx.config.readerFontSize) return;
    Reader.setFontSize(ctx.config.readerFontSize);  // shows a status screen if it has to paginate
    LibraryManager::getInstance().flushReaderPosition();   // same place, new page numbers
}

void returnToReading(AppContext& ctx) {
    commitFontSize(ctx);
    ctx.currentState = STATE_READING;
    Reader.renderCurrentPage();
}

// Saves the position, pushes unsynced progress, closes the book and goes home.
void closeBookAndGoHome(AppContext& ctx, bool fullRefresh) {
    auto& lib = LibraryManager::getInstance();
    const uint32_t t0 = millis();
    lib.flushReaderPosition();
    Serial.printf("[Reader] Close: position saved in %lu ms\n", (unsigned long)(millis() - t0));
    // Unsynced position: uploaded in the background (Settings: send progress when
    // closing a book) while the home screen is already up; otherwise it waits
    // for the next sync.
    const bool upload = ctx.config.syncOnClose && lib.currentRecord().state.localDirty;
    const int fileId = lib.lastBook.fileId;
    const String path = lib.lastBook.filePath;
    Reader.closeBook();
    Reader.setFontSize(ctx.config.readerFontSize);  // closed: no pagination, used by the next open
    ctx.currentState = STATE_HOME;
    drawHomeScreen(ctx, fullRefresh);
    if (upload) SyncService::startBackgroundUpload(ctx, fileId, path);
}

void executeMenuAction(AppContext& ctx, int index) {
    switch (index) {
        case 0:  // Оглавление
            commitFontSize(ctx);  // chapter page numbers depend on the font
            if (Reader.hasChapters()) {
                ctx.currentState = STATE_TOC;
                ctx.selectedTocIndex = Reader.getCurrentChapterIndex();
                drawTocScreen(ctx, true);
            } else {
                Modal.showMessage(tr(MsgId::READER_TOC_TITLE), tr(MsgId::READER_TOC_NOT_FOUND_L1), tr(MsgId::READER_TOC_NOT_FOUND_L2), "", "",
                                  1500, /*fullRedraw=*/true);
            }
            break;
        case 1:  // Шрифт: submenu of sizes
            ctx.selectedFontIndex = ctx.config.readerFontSize;
            ctx.currentState = STATE_FONT_MENU;
            drawFontMenuScreen(ctx, false);
            break;
        case 2:  // Синхронизация
            commitFontSize(ctx);
            SyncService::syncWithBookOrbit(ctx);
            break;
        case 3:  // Продолжить чтение
            returnToReading(ctx);
            break;
        case 4:  // Закрыть книгу
            closeBookAndGoHome(ctx, true);
            break;
    }
}

}  // namespace

namespace ReaderScreen {

bool openBook(AppContext& ctx, const String& path, const String& title, const String& author,
              const String& coverPath, float progress, int page, const String& xpath,
              const String& docId, int fileId) {
    auto& lib = LibraryManager::getInstance();
    auto& store = ProgressStore::instance();
    Buttons.clearEvents();
    Serial.printf("[Reader] Opening book: %s (file %d)\n", path.c_str(), fileId);
    if (!Reader.openBook(path)) {
        if (Reader.lastFailedForMemory()) {
            Modal.showMessage(tr(MsgId::COMMON_ERROR), tr(MsgId::READER_OPEN_ERR_L1),
                              tr(MsgId::READER_OPEN_ERR_L2), "", "", 4000, false, tr(MsgId::READER_OPEN_ERR_FOOTER));
        } else {
            Modal.showMessage(tr(MsgId::READER_OPEN_ERR_TITLE), tr(MsgId::READER_OPEN_ERR_L1),
                              tr(MsgId::READER_OPEN_ERR_L2), "", "", 3500, false, tr(MsgId::READER_OPEN_ERR_FOOTER));
        }
        return false;
    }
    MemStats::log("book opened");
    MemStats::probeEngine("book opened");
    MemStats::logMap("book opened");

    // This device's position for the book if it was opened here before; otherwise
    // the position the caller got from the server (catalog snapshot).
    const BookProgressRecord* knownRec = store.find(fileId, path);
    const bool known = knownRec != nullptr;
    const uint32_t tPos = millis();
    if (knownRec) {
        Serial.printf("[Reader] Restoring position: cfi '%s', xpath '%s', %.1f%%, page %d\n",
                      knownRec->state.local.cfi.c_str(), knownRec->state.local.xpath.c_str(),
                      knownRec->state.local.percent * 100.0f, knownRec->state.local.page);
        const BookProgressRecord* known = knownRec;
        Reader.goToPosition(known->state.local.cfi.c_str(), known->state.local.xpath.c_str(),
                            known->state.local.percent, known->state.local.page);
    } else {
        Reader.goToPosition("", xpath, progress, page);
    }
    Serial.printf("[Reader] Position restored in %lu ms\n", (unsigned long)(millis() - tPos));

    lib.lastBook.filePath = path;
    lib.lastBook.title = title;
    lib.lastBook.author = author;
    lib.lastBook.coverPath = LibraryManager::resolveCoverPath(path, title, coverPath, true);
    lib.lastBook.fileId = fileId;
    lib.lastBook.docId = docId;   // KOReader document hash: kept for a future KOSync, not computed now
    for (const auto& b : lib.masterBooks) {   // server ids from the library list (exact match)
        if (lib.lastBook.fileId <= 0 && b.filePath.equalsIgnoreCase(path) && b.fileId > 0) lib.lastBook.fileId = b.fileId;
    }

    BookProgressRecord& rec = lib.currentRecord();
    if (!known) {
        // First time on this device: start from where the reader is now (not a user move).
        rec.state.local.percent = ProgressSync::hasProgress(progress) ? progress : Reader.getProgress();
        rec.state.local.xpath = xpath.c_str();
    }
    const uint32_t tFlush = millis();
    lib.flushReaderPosition();   // page/xpath for the record and lastBook, saves both files
    Serial.printf("[Reader] Position saved in %lu ms\n", (unsigned long)(millis() - tFlush));

    returnToReading(ctx);
    Buttons.clearEvents();
    return true;
}

bool resumeLastBook(AppContext& ctx) {
    auto& lib = LibraryManager::getInstance();
    if (lib.lastBook.filePath.length() == 0 || !Storage.exists(lib.lastBook.filePath.c_str())) return false;
    Serial.printf("[Power] Instant resume book: %s (page %d)\n", lib.lastBook.title.c_str(), lib.lastBook.page);
    if (!Reader.openBook(lib.lastBook.filePath)) return false;

    // The book's record has the most exact position (a CFI after a pull from the web reader).
    const ProgressSync::Position& pos = lib.currentRecord().state.local;
    if (pos.cfi.empty() && pos.xpath.empty() && !ProgressSync::hasProgress(pos.percent)) {
        Reader.goToPosition("", lib.lastBook.xpath, lib.lastBook.progress, lib.lastBook.page);
    } else {
        Reader.goToPosition(pos.cfi.c_str(), pos.xpath.c_str(), pos.percent, pos.page);
    }
    returnToReading(ctx);
    return true;
}

// Picco has only up/down keys (no left/right): there they turn pages, and
// chapters are reached through the table of contents.
#if defined(FREEINK_DEVICE_PICCO) && FREEINK_DEVICE_PICCO
constexpr bool kUpDownTurnPages = true;
#else
constexpr bool kUpDownTurnPages = false;
#endif

void handleReadingEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    auto& lib = LibraryManager::getInstance();

    ButtonId id = event.id;
    if (kUpDownTurnPages) {
        if (id == BTN_DOWN) id = BTN_RIGHT;
        else if (id == BTN_UP) id = BTN_LEFT;
    }

    switch (id) {
        case BTN_RIGHT:
            if (Reader.nextPage()) lib.recordReaderMove(/*withXPath=*/false, /*persist=*/false);
            break;
        case BTN_LEFT:
            if (Reader.prevPage()) lib.recordReaderMove(false, false);
            break;
        case BTN_DOWN:
            if (Reader.nextChapter()) lib.recordReaderMove(true, true);
            break;
        case BTN_UP:
            if (Reader.prevChapter()) lib.recordReaderMove(true, true);
            break;
        case BTN_CONFIRM:
            ctx.currentState = STATE_READER_MENU;
            ctx.selectedReaderMenuIndex = 0;
            drawReaderMenuScreen(ctx, true);
            break;
        case BTN_BACK:
            closeBookAndGoHome(ctx, false);
            break;
        default:
            break;
    }
}

void handleTocEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    auto& lib = LibraryManager::getInstance();

    if (event.id == BTN_DOWN || event.id == BTN_UP) {
        int prev = ctx.selectedTocIndex;
        if (event.id == BTN_DOWN && ctx.selectedTocIndex + 1 < Reader.getChapterCount()) ctx.selectedTocIndex++;
        if (event.id == BTN_UP && ctx.selectedTocIndex > 0) ctx.selectedTocIndex--;
        if (prev != ctx.selectedTocIndex) {
            bool pageChanged = (prev / TOC_ITEMS_PER_PAGE != ctx.selectedTocIndex / TOC_ITEMS_PER_PAGE);
            drawTocScreen(ctx, pageChanged);
        }
    } else if (event.id == BTN_CONFIRM) {
        if (Reader.getChapterCount() > 0) {
            Reader.goToChapter(ctx.selectedTocIndex);
            lib.recordReaderMove(true, true);
        }
        returnToReading(ctx);
    } else if (event.id == BTN_BACK) {
        ctx.currentState = STATE_READER_MENU;
        drawReaderMenuScreen(ctx, true);
    }
}

void handleMenuEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    switch (event.id) {
        case BTN_DOWN:
            ctx.selectedReaderMenuIndex = (ctx.selectedReaderMenuIndex + 1) % NUM_READER_MENU_ITEMS;
            drawReaderMenuScreen(ctx, false);
            break;
        case BTN_UP:
            ctx.selectedReaderMenuIndex = (ctx.selectedReaderMenuIndex - 1 + NUM_READER_MENU_ITEMS) % NUM_READER_MENU_ITEMS;
            drawReaderMenuScreen(ctx, false);
            break;
        case BTN_LEFT:
            returnToReading(ctx);
            break;
        case BTN_RIGHT:
            executeMenuAction(ctx, ctx.selectedReaderMenuIndex);
            break;
        case BTN_CONFIRM:
            executeMenuAction(ctx, ctx.selectedReaderMenuIndex);
            break;
        case BTN_BACK:
            returnToReading(ctx);
            break;
        default:
            break;
    }
}

void handleFontMenuEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    int& sel = ctx.selectedFontIndex;
    switch (event.id) {
        case BTN_DOWN:
            sel = (sel + 1) % FontEngine::kSizeCount;
            drawFontMenuScreen(ctx, false);
            break;
        case BTN_UP:
            sel = (sel - 1 + FontEngine::kSizeCount) % FontEngine::kSizeCount;
            drawFontMenuScreen(ctx, false);
            break;
        case BTN_CONFIRM:
        case BTN_RIGHT:
            // Apply and go back to the page (re-paginates once, with a status screen if needed).
            if (sel != ctx.config.readerFontSize) {
                ctx.config.readerFontSize = sel;
                ctx.config.save();
            }
            returnToReading(ctx);
            break;
        case BTN_BACK:
        case BTN_LEFT:
            ctx.currentState = STATE_READER_MENU;
            drawReaderMenuScreen(ctx, false);
            break;
        default:
            break;
    }
}

}  // namespace ReaderScreen
