#include "screens.h"
#include <algorithm>
#include "appearance.h"
#include "../orientation.h"
#include "status_bar_menu.h"
#include "../../services/bookmarks.h"
#include "../../ui/font.h"
#include "../../platform/hal/mem_stats.h"
#include "../../convert/sd_convert.h"
#include "../../reader/reader.h"
#include "../../platform/hal/storage.h"
#include "../../services/library_manager.h"
#include "../../storage/progress_store.h"
#include "../../core/progress_sync.h"
#include "../../services/sync_service.h"
#include "../../services/power_service.h"
#include "../../ui/modal.h"
#include "../../ui/text_utils.h"
#include "../../ui/views/home_view.h"
#include "../../ui/views/reader_menu_view.h"
#include "../../ui/views/settings_view.h"
#include "../../ui/views/status_modal.h"
#include "../../reader/font_store.h"
#include "../../ui/i18n.h"
#include "../../ui/kit/ui_kit.h"
#include "../../ui/actions.h"
#include "../../services/wifi_session.h"
#include "../../platform/device.h"
#include "../../platform/hal/imu.h"
#include "../app_controller.h"

namespace {

// The menus only change the settings; the book is laid out again once, when
// they are left (not on every step through the sizes and spacings).
void commitFontSize(AppContext& ctx) {
    Reader.setFullRefreshEvery(ctx.config.fullRefreshEvery);
    Reader.setSharpImages(ctx.config.sharpImages);
    const bool size = Reader.getFontSize() != ctx.config.readerFontSize;
    const bool layout = !(Reader.layoutOptions() == Appearance::layoutOptions(ctx.config));
    // The pages' turn is the interface's and the book's together (applyOrientation).
    const bool turned = Reader.orientation() != ((ctx.config.uiOrientation + ctx.config.orientation) & 3);
    const bool bar = !(Reader.statusBar() == StatusBarMenu::options(ctx.config));
    if (!size && !layout && !turned && !bar) return;
    Reader.setStatusBar(StatusBarMenu::options(ctx.config));
    applyOrientation(ctx.config.uiOrientation, ctx.config.orientation);
    Reader.setLayoutOptions(Appearance::layoutOptions(ctx.config));
    Reader.setFontSize(ctx.config.readerFontSize);  // shows a status screen if it has to paginate
    LibraryManager::getInstance().flushReaderPosition();   // same place, new page numbers
}

void returnToReading(AppContext& ctx) {
    commitFontSize(ctx);
    ctx.currentState = STATE_READING;
    Reader.renderCurrentPage();
}

// Saves the position, pushes unsynced progress, closes the book and goes home
// (drawn here, or by the dialog that called it: `draw` false).
void closeBookAndGoHome(AppContext& ctx, bool fullRefresh, bool draw = true) {
    auto& lib = LibraryManager::getInstance();
    const uint32_t t0 = millis();
    lib.flushReaderPosition();
    Serial.printf("[Reader] Close: position saved in %lu ms\n", (unsigned long)(millis() - t0));
    // Unsynced position: uploaded in the background (Settings: send progress when
    // closing a book) while the home screen is already up; otherwise it waits
    // for the next sync.
    const bool upload = ctx.config.syncOnClose && !LibraryManager::isOfflinePath(lib.lastBook.filePath) &&
                        lib.currentRecord().state.localDirty;
    const int fileId = lib.lastBook.fileId;
    const String path = lib.lastBook.filePath;
    Reader.closeBook();
    Bookmarks.clear();
    Reader.setFontSize(ctx.config.readerFontSize);  // closed: no pagination, used by the next open
    lib.ensureScanned();   // woken up straight into the book: the library was not scanned yet
    ctx.currentState = STATE_HOME;
    if (draw) drawHomeScreen(ctx, fullRefresh);
    if (upload) SyncService::startBackgroundUpload(ctx, fileId, path);
}

void executeMenuAction(AppContext& ctx, int index) {
    switch (index) {
        case 0:  // Оглавление
            commitFontSize(ctx);  // chapter page numbers depend on the font
            if (Reader.hasChapters()) {
                ctx.currentState = STATE_TOC;
                // Before the first chapter (a title page) there is no current one: the first row.
                ctx.selectedTocIndex = std::max(0, Reader.getCurrentChapterIndex());
                drawTocScreen(ctx, uikit::periodicFullRefresh());
            } else {
                Modal.showMessage(tr(MsgId::READER_TOC_TITLE), tr(MsgId::READER_TOC_NOT_FOUND_L1),
                                  tr(MsgId::READER_TOC_NOT_FOUND_L2), "", "", 1500, /*fullRedraw=*/true);
            }
            break;
        case 1:  // Оформление
            Reader.stopBackground();   // its memory lays out the preview
            ctx.currentState = STATE_APPEARANCE_MENU;
            drawAppearanceScreen(ctx, false);
            break;
        case 2:  // Закладки
            commitFontSize(ctx);   // pages as they will be read
            ctx.selectedBookmarkIndex = 0;
            placeBookmarks(Bookmarks);
            ctx.currentState = STATE_BOOKMARKS;
            drawBookmarksScreen(ctx, false);
            break;
        case 3:  // Синхронизация
            commitFontSize(ctx);
            SyncService::syncWithBookOrbit(ctx);
            break;
        case 4:  // Продолжить чтение
            returnToReading(ctx);
            break;
        case 5:  // Закрыть книгу
            closeBookAndGoHome(ctx, uikit::periodicFullRefresh());
            break;
    }
}

}  // namespace

namespace ReaderScreen {

bool openBook(AppContext& ctx, String path, String title, String author, String coverPath, float progress, int page,
              String xpath, String docId, int fileId) {
    auto& lib = LibraryManager::getInstance();
    auto& store = ProgressStore::instance();
    Buttons.clearEvents();
    Serial.printf("[Reader] Opening book: %s (file %d)\n", path.c_str(), fileId);
    WifiSession::giveMemoryToReader();   // B9: no upload or radio while the index is built (small tier)
    // The arguments own their strings: releasing a selected BookItem must not
    // leave dangling references to its path, title, format or saved position.
    int bookId = 0;
    for (const auto& b : lib.masterBooks) {
        if (!b.filePath.equalsIgnoreCase(path)) continue;
        if (fileId <= 0 && b.fileId > 0) fileId = b.fileId;
        bookId = b.bookId;
        break;
    }
    if (lib.releaseListsForReader()) MemStats::log("reader: library lists released");
    bool opened = Reader.openBook(path);
    if (!opened && Reader.lastFailedForMemory() && Platform::kMemory.restartWhenOutOfMemory) {
        // P2: right after a download the radio's memory comes back a moment after
        // Wi-Fi is off. Once more when the heap has settled, before a restart.
        Serial.println("[Reader] Short of memory: trying again once the heap has settled.");
        WifiSession::waitForHeapToSettle();
        opened = Reader.openBook(path);
    }
    if (!opened) {
        const auto why = (convert::ConvertProblem)Reader.lastConvertProblem();
        if (why == convert::ConvertProblem::Drm || why == convert::ConvertProblem::Unsupported) {
            lib.ensureScanned();   // the failed open returns to the original library/home screen
            Modal.showMessage(tr(MsgId::READER_OPEN_ERR_TITLE), tr(MsgId::READER_OPEN_ERR_L1),
                              tr(why == convert::ConvertProblem::Drm ? MsgId::READER_OPEN_DRM
                                                                     : MsgId::READER_OPEN_VARIANT),
                              "", "", 4000, false, tr(MsgId::READER_OPEN_ERR_FOOTER));
        } else if (Reader.lastFailedForMemory()) {
            // The heap is cut up (typically right after a download over Wi-Fi): a
            // restart gives it back whole, and the book opens straight away, as
            // after sleep. Only on the device; the message below otherwise.
            lib.lastBook.filePath = path;
            lib.lastBook.title = title;
            lib.lastBook.author = author;
            lib.lastBook.coverPath = coverPath;
            lib.lastBook.fileId = fileId;
            lib.lastBook.docId = docId;
            // This book's position (the previous book's stayed in lastBook): the
            // record, if it has one, wins when it is resumed after the restart.
            lib.lastBook.progress = progress;
            lib.lastBook.page = page > 0 ? page : 1;
            lib.lastBook.xpath = xpath;
            lib.lastBook.totalPages = 1;
            lib.saveLastBook();
            PowerService::restartIntoLastBook();
            lib.ensureScanned();   // restart unavailable (e.g. simulator): restore the screen's data
            Modal.showMessage(tr(MsgId::COMMON_ERROR), tr(MsgId::READER_OPEN_ERR_L1), tr(MsgId::READER_OPEN_ERR_MEMORY),
                              "", "", 4000, false, tr(MsgId::READER_OPEN_ERR_FOOTER));
        } else {
            lib.ensureScanned();
            Modal.showMessage(tr(MsgId::READER_OPEN_ERR_TITLE), tr(MsgId::READER_OPEN_ERR_L1),
                              tr(MsgId::READER_OPEN_ERR_L2), "", "", 3500, false, tr(MsgId::READER_OPEN_ERR_FOOTER));
        }
        return false;
    }
    Bookmarks.load(path);
    placeBookmarks(Bookmarks);
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
    // Keep the server book id captured before lending the lists to the reader.
    BookProgressRecord& rec = store.getOrCreate(fileId, bookId, path, title);
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
    Bookmarks.load(lib.lastBook.filePath);
    placeBookmarks(Bookmarks);

    // The book's record has the most exact position (a CFI after a pull from the web reader).
    const ProgressSync::Position& pos = lib.currentRecord().state.local;
    if (pos.cfi.empty() && pos.xpath.empty() && !ProgressSync::hasProgress(pos.percent)) {
        Reader.goToPosition("", lib.lastBook.xpath, lib.lastBook.progress, lib.lastBook.page);
    } else {
        Reader.goToPosition(pos.cfi.c_str(), pos.xpath.c_str(), pos.percent, pos.page);
    }
    Reader.requestFullRefresh();   // the sleep cover is on the screen
    returnToReading(ctx);
    return true;
}

void handleReadingEvent(AppContext& ctx, const ButtonEvent& event) {
    // Boards with only up/down keys (no left/right) turn pages with them; chapters
    // are reached through the table of contents (Actions::reading).
    const Action action = Actions::reading(event, ctx.config.upDownTurnPages);
    if (action == Action::Menu) {   // a held BACK / capacitive Home key: the reader menu at once
        ctx.currentState = STATE_READER_MENU;
        ctx.selectedReaderMenuIndex = 0;
        drawReaderMenuScreen(ctx, false);
        return;
    }
    auto& lib = LibraryManager::getInstance();

    switch (action) {
        case Action::Next:
            if (Reader.nextPage()) {
                lib.recordReaderMove(/*withXPath=*/false, /*persist=*/false);
            } else if (Reader.atEnd()) {
                // The last page (U2): close the book, or stay on it.
                Serial.println("[Reader] End of the book");
                Modal.showConfirm(
                        tr(MsgId::READER_END_TITLE), utf8Prefix(lib.lastBook.title, 64), tr(MsgId::READER_END_L1), "",
                        tr(MsgId::READER_END_FOOTER), [&ctx]() { closeBookAndGoHome(ctx, false, /*draw=*/false); },
                        nullptr, tr(MsgId::READER_END_CLOSE));
            }
            break;
        case Action::Prev:
            if (Reader.prevPage()) lib.recordReaderMove(false, false);
            break;
        // A jump (a chapter here, the table of contents, a bookmark) is saved at
        // once; BACK closes the book from wherever it is (no going back, B6).
        case Action::Down:
            if (Reader.nextChapter()) lib.recordReaderMove(true, true);
            break;
        case Action::Up:
            if (Reader.prevChapter()) lib.recordReaderMove(true, true);
            break;
        case Action::Select:
            ctx.currentState = STATE_READER_MENU;
            ctx.selectedReaderMenuIndex = 0;
            // A fast refresh (0.5 s instead of 3.8 s); every 8th screen change is a full one.
            drawReaderMenuScreen(ctx, uikit::periodicFullRefresh());
            break;
        case Action::Back:
            closeBookAndGoHome(ctx, false);
            break;
        default:
            break;
    }
}

void handleTocEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const Action key = Actions::of(event);
    auto& lib = LibraryManager::getInstance();

    if (key == Action::Down || key == Action::Up || key == Action::Prev || key == Action::Next) {
        const int prev = ctx.selectedTocIndex;
        const int perPage = std::max(1, tocRowsPerPage());   // as many rows as the last drawing fitted
        const int n = Reader.getChapterCount();
        // UP/DOWN a row (a page at a time once held for a while), LEFT/RIGHT a page.
        const bool byPage = key == Action::Prev || key == Action::Next || event.repeat >= 4;
        const bool down = key == Action::Down || key == Action::Next;
        int to = prev;
        if (byPage)
            to = down ? (prev / perPage + 1) * perPage : (prev / perPage - (prev % perPage == 0 ? 1 : 0)) * perPage;
        else
            to = down ? prev + 1 : prev - 1;
        ctx.selectedTocIndex = std::max(0, std::min(to, n - 1));
        if (prev != ctx.selectedTocIndex) {
            const bool pageChanged = (prev / perPage != ctx.selectedTocIndex / perPage);
            drawTocScreen(ctx, pageChanged && uikit::periodicFullRefresh());
        }
    } else if (key == Action::Select) {
        if (Reader.getChapterCount() > 0) {
            Reader.goToChapter(ctx.selectedTocIndex);
            lib.recordReaderMove(true, true);
        }
        returnToReading(ctx);
    } else if (key == Action::Back) {
        ctx.currentState = STATE_READER_MENU;
        drawReaderMenuScreen(ctx, uikit::periodicFullRefresh());
    }
}

void handleMenuEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    switch (Actions::of(event)) {
        case Action::Down:
            ctx.selectedReaderMenuIndex = (ctx.selectedReaderMenuIndex + 1) % readerMenuItemCount();
            drawReaderMenuScreen(ctx, false);
            break;
        case Action::Up:
            ctx.selectedReaderMenuIndex =
                    (ctx.selectedReaderMenuIndex - 1 + readerMenuItemCount()) % readerMenuItemCount();
            drawReaderMenuScreen(ctx, false);
            break;
        case Action::Prev:
            returnToReading(ctx);
            break;
        case Action::Next:
            executeMenuAction(ctx, ctx.selectedReaderMenuIndex);
            break;
        case Action::Select:
            executeMenuAction(ctx, ctx.selectedReaderMenuIndex);
            break;
        case Action::Back:
            returnToReading(ctx);
            break;
        default:
            break;
    }
}

void handleFontMenuEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    int& sel = ctx.selectedFontIndex;
    switch (Actions::of(event)) {
        case Action::Down:
            sel = (sel + 1) % FontEngine::kSizeCount;
            drawFontMenuScreen(ctx, false);
            break;
        case Action::Up:
            sel = (sel - 1 + FontEngine::kSizeCount) % FontEngine::kSizeCount;
            drawFontMenuScreen(ctx, false);
            break;
        case Action::Select:
        case Action::Next:
            // Chosen: back to Appearance (laid out once, when it is left).
            if (sel != ctx.config.readerFontSize) {
                ctx.config.readerFontSize = sel;
                ctx.config.save();
            }
            ctx.currentState = STATE_APPEARANCE_MENU;
            drawAppearanceScreen(ctx, false);
            break;
        case Action::Back:
        case Action::Prev:
            ctx.currentState = STATE_APPEARANCE_MENU;
            drawAppearanceScreen(ctx, false);
            break;
        default:
            break;
    }
}

static void drawInstallProgress(void* name, int permille) {
    static int s_lastStep = -1;
    static uint32_t s_lastAt = 0;
    const int step = permille / 100;
    // An e-ink refresh blocks ~0.5 s: at most every 3 s (and at the start).
    if (permille < 1000 && (step == s_lastStep || (s_lastStep >= 0 && permille > 0 && millis() - s_lastAt < 3000)))
        return;
    if (permille == 0) s_lastStep = -1;
    s_lastStep = step;
    s_lastAt = millis();
    char line[64];
    snprintf(line, sizeof(line), tr(MsgId::TYPEFACE_INSTALL_FMT), permille / 10);
    drawStatusModal(tr(MsgId::TYPEFACE_INSTALL_TITLE), static_cast<const String*>(name)->c_str(), line);
}

void handleTypefaceMenuEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const bool sources = ctx.typefaceStorage == 0;
    const auto& fams = typefaceFamilies(false);
    const int n = sources ? 3 : (int)fams.size();
    int& sel = ctx.selectedTypefaceIndex;
    switch (Actions::of(event)) {
        case Action::Down:
        case Action::Up:
            if (n) sel = (sel + (Actions::of(event) == Action::Down ? 1 : n - 1)) % n;
            drawTypefaceMenuScreen(ctx, false);
            break;
        case Action::Select:
        case Action::Next: {
            if (sources && sel > 0) {
                ctx.typefaceStorage = sel;
                sel = 0;
                typefaceFamilies(true, ctx.typefaceStorage);
                drawTypefaceMenuScreen(ctx, false);
                return;
            }
            if (!sources && (sel < 0 || sel >= n)) return;
            String family;
            if (!sources) {
                const FontStore::Family fam = fams[sel];
                if (!FontStore::contains(fam.name)) {
                    Reader.setFontFamily("");
                    String name = fam.name;
                    drawInstallProgress(&name, 0);
                    if (!FontStore::install(fam, &drawInstallProgress, &name)) {
                        Reader.setFontFamily(ctx.config.readerFont);
                        LibraryManager::getInstance().flushReaderPosition();
                        typefaceFamilies(true, ctx.typefaceStorage);
                        Modal.showMessage(tr(MsgId::TYPEFACE_MENU_TITLE), tr(MsgId::TYPEFACE_INSTALL_FAILED), fam.name);
                        return;
                    }
                }
                family = fam.name;
            }
            if (Reader.setFontFamily(family)) LibraryManager::getInstance().flushReaderPosition();
            if (ctx.config.readerFont != family) {
                ctx.config.readerFont = family;
                ctx.config.save();
            }
            ctx.currentState = STATE_APPEARANCE_MENU;
            drawAppearanceScreen(ctx, false);
            break;
        }
        case Action::Prev:
        case Action::Delete:
            if (!sources && sel >= 0 && sel < n) {
                const String name = fams[sel].name;
                const int storage = ctx.typefaceStorage;
                Modal.showConfirm(
                        tr(MsgId::KEY_DELETE), name,
                        tr(storage == 1 ? MsgId::TYPEFACE_FLASH_TITLE : MsgId::TYPEFACE_SD_TITLE),
                        tr(MsgId::TYPEFACE_DELETE_CONFIRM), "",
                        [&ctx, name, storage]() {
                            Reader.setFontFamily("");
                            const bool ok =
                                    storage == 1 ? FontStore::removeInstalled(name) : FontStore::removeCard(name);
                            if (FontStore::sameFamilyName(ctx.config.readerFont, name) && !FontStore::contains(name)) {
                                ctx.config.readerFont = "";
                                ctx.config.save();
                            }
                            Reader.setFontFamily(ctx.config.readerFont);
                            LibraryManager::getInstance().flushReaderPosition();
                            typefaceFamilies(true, storage);
                            if (!ok) Modal.showMessage(tr(MsgId::KEY_DELETE), tr(MsgId::TYPEFACE_DELETE_FAILED), name);
                        },
                        nullptr, tr(MsgId::KEY_DELETE));
            }
            break;
        case Action::Back:
            if (!sources) {
                sel = ctx.typefaceStorage;
                ctx.typefaceStorage = 0;
                drawTypefaceMenuScreen(ctx, false);
            } else {
                ctx.currentState = STATE_APPEARANCE_MENU;
                drawAppearanceScreen(ctx, false);
            }
            break;
        default:
            break;
    }
}

static void openTypefaces(AppContext& ctx) {
    ctx.typefaceStorage = 0;
    ctx.selectedTypefaceIndex = 0;
    ctx.currentState = STATE_TYPEFACE_MENU;
    drawTypefaceMenuScreen(ctx, false);
}

void handleAppearanceEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const Action key = Actions::onValue(Actions::of(event));
    int& sel = ctx.selectedAppearanceIndex;
    const int row = Appearance::rowAt(sel);
    switch (key) {
        case Action::Down:
            sel = (sel + 1) % Appearance::count();
            drawAppearanceScreen(ctx, false);
            break;
        case Action::Up:
            sel = (sel - 1 + Appearance::count()) % Appearance::count();
            drawAppearanceScreen(ctx, false);
            break;
        case Action::Less:
        case Action::More:
        case Action::Select:
            if (row == Appearance::TYPEFACE && key != Action::Less) {
                openTypefaces(ctx);
            } else if (row == Appearance::SIZE && key == Action::Select) {
                ctx.selectedFontIndex = ctx.config.readerFontSize;
                ctx.currentState = STATE_FONT_MENU;
                drawFontMenuScreen(ctx, false);
            } else if (Appearance::step(ctx.config, row, key == Action::Less ? -1 : 1)) {
                ctx.config.save();
                drawAppearanceScreen(ctx, false);   // (the book turns when the menus are left)
            }
            break;
        case Action::Back:
            returnToReading(ctx);   // laid out once, with everything changed here
            break;
        default:
            break;
    }
}

void handleStatusBarEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const Action key = Actions::onValue(Actions::of(event));
    int& sel = ctx.selectedStatusBarIndex;
    switch (key) {
        case Action::Down:
            sel = (sel + 1) % StatusBarMenu::COUNT;
            drawStatusBarScreen(ctx, false);
            break;
        case Action::Up:
            sel = (sel - 1 + StatusBarMenu::COUNT) % StatusBarMenu::COUNT;
            drawStatusBarScreen(ctx, false);
            break;
        case Action::Less:
        case Action::More:
        case Action::Select:
            StatusBarMenu::step(ctx.config, sel, key == Action::Less ? -1 : 1);
            ctx.config.save();
            drawStatusBarScreen(ctx, false);
            break;
        case Action::Back:   // Settings -> Reading (the book gets it when it is opened)
            ctx.currentState = STATE_READING_SETTINGS;
            drawReadingSettingsScreen(ctx, false);
            break;
        default:
            break;
    }
}

void handleBookmarksEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const int n = 1 + Bookmarks.count();
    int& sel = ctx.selectedBookmarkIndex;
    switch (Actions::of(event)) {
        case Action::Down:
            sel = (sel + 1) % n;
            drawBookmarksScreen(ctx, false);
            break;
        case Action::Up:
            sel = (sel - 1 + n) % n;
            drawBookmarksScreen(ctx, false);
            break;
        case Action::Select:
        case Action::Next:
            if (sel == 0) {
                // This page's: removed if there is one, else added.
                const int here = bookmarkOnPage();
                if (here >= 0) {
                    Bookmarks.remove(here);
                } else {
                    const BookReader::Position pos = Reader.position();
                    Bookmark b;
                    b.spine = pos.spine;
                    b.chars = pos.chars;
                    b.percent = Reader.getProgress();
                    b.chapter = Reader.getCurrentChapterTitle();
                    b.snippet = Reader.pageSnippet();
                    b.cfi = Reader.getCfi();   // for the web reader (uploaded at the next sync)
                    Bookmarks.add(b);
                    Serial.printf("[Bookmarks] added at %d:%u (%.1f%%)\n", b.spine, (unsigned)b.chars,
                                  b.percent * 100.0f);
                }
                Bookmarks.save();
                returnToReading(ctx);   // the ribbon shows on the page
            } else if (sel <= Bookmarks.count()) {
                const Bookmark& b = Bookmarks.items()[sel - 1];
                if (!b.placed()) {
                    // From the server and not placed yet (its chapter could not be read).
                    if (!Reader.setPageByCfi(b.cfi, 0.0f)) break;
                } else {
                    BookReader::Position pos;
                    pos.spine = b.spine;
                    pos.chars = b.chars;
                    Reader.goTo(pos);
                }
                LibraryManager::getInstance().recordReaderMove(true, true);
                returnToReading(ctx);
            }
            break;
        case Action::Prev:     // LEFT, or the "delete" beside a bookmark on touch boards
        case Action::Delete:
            if (sel > 0 && sel <= Bookmarks.count()) {
                Serial.printf("[Bookmarks] removed #%d\n", sel);
                Bookmarks.remove(sel - 1);
                Bookmarks.save();
                if (sel > Bookmarks.count()) sel = Bookmarks.count();
                drawBookmarksScreen(ctx, false);
            }
            break;
        case Action::Back:
            ctx.currentState = STATE_READER_MENU;
            drawReaderMenuScreen(ctx, false);
            break;
        default:
            break;
    }
}

}  // namespace ReaderScreen
