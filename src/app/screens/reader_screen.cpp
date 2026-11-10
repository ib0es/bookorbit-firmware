#include "screens.h"
#include "appearance.h"
#include "../orientation.h"
#include "status_bar_menu.h"
#include "../../services/bookmarks.h"
#include "../../ui/font.h"
#include "../../hal/mem_stats.h"
#include "../../convert/sd_convert.h"
#include "../../reader/reader.h"
#include "../../hal/storage.h"
#include "../../services/library_manager.h"
#include "../../services/progress_store.h"
#include "../../services/progress_sync.h"
#include "../../services/sync_service.h"
#include "../../ui/modal.h"
#include "../../ui/views/home_view.h"
#include "../../ui/views/reader_menu_view.h"
#include "../../ui/views/status_modal.h"
#include "../../reader/font_store.h"
#include "../../ui/i18n.h"

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
    Bookmarks.clear();
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
        const auto why = (convert::ConvertProblem)Reader.lastConvertProblem();
        if (why == convert::ConvertProblem::Drm || why == convert::ConvertProblem::Unsupported) {
            Modal.showMessage(tr(MsgId::READER_OPEN_ERR_TITLE), tr(MsgId::READER_OPEN_ERR_L1),
                              tr(why == convert::ConvertProblem::Drm ? MsgId::READER_OPEN_DRM : MsgId::READER_OPEN_VARIANT),
                              "", "", 4000, false, tr(MsgId::READER_OPEN_ERR_FOOTER));
        } else if (Reader.lastFailedForMemory()) {
            Modal.showMessage(tr(MsgId::COMMON_ERROR), tr(MsgId::READER_OPEN_ERR_L1),
                              tr(MsgId::READER_OPEN_ERR_L2), "", "", 4000, false, tr(MsgId::READER_OPEN_ERR_FOOTER));
        } else {
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
    Bookmarks.load(lib.lastBook.filePath);
    placeBookmarks(Bookmarks);

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
            const int perPage = tocRowsPerPage();   // as many rows as the last drawing fitted
            const bool pageChanged = (prev / perPage != ctx.selectedTocIndex / perPage);
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
            // Chosen: back to Appearance (laid out once, when it is left).
            if (sel != ctx.config.readerFontSize) {
                ctx.config.readerFontSize = sel;
                ctx.config.save();
            }
            ctx.currentState = STATE_APPEARANCE_MENU;
            drawAppearanceScreen(ctx, false);
            break;
        case BTN_BACK:
        case BTN_LEFT:
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
    if (permille < 1000 && (step == s_lastStep || (s_lastStep >= 0 && permille > 0 && millis() - s_lastAt < 3000))) return;
    if (permille == 0) s_lastStep = -1;
    s_lastStep = step;
    s_lastAt = millis();
    char line[64];
    snprintf(line, sizeof(line), tr(MsgId::TYPEFACE_INSTALL_FMT), permille / 10);
    drawStatusModal(tr(MsgId::TYPEFACE_INSTALL_TITLE), static_cast<const String*>(name)->c_str(), line);
}

void handleTypefaceMenuEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const auto& fams = typefaceFamilies(false);
    const int n = 1 + (int)fams.size();
    int& sel = ctx.selectedTypefaceIndex;
    switch (event.id) {
        case BTN_DOWN:
            sel = (sel + 1) % n;
            drawTypefaceMenuScreen(ctx, false);
            break;
        case BTN_UP:
            sel = (sel - 1 + n) % n;
            drawTypefaceMenuScreen(ctx, false);
            break;
        case BTN_CONFIRM:
        case BTN_RIGHT: {
            String family;   // "" = built-in
            if (sel > 0 && sel <= (int)fams.size()) {
                const FontStore::Family& fam = fams[sel - 1];
                if (FontStore::installedName() != fam.name) {
                    // Into the flash partition first: the faces in use there are let go of.
                    Reader.setFontFamily("");
                    String name = fam.name;
                    drawInstallProgress(&name, 0);
                    if (!FontStore::install(fam, &drawInstallProgress, &name)) {
                        Modal.showMessage(tr(MsgId::TYPEFACE_MENU_TITLE), tr(MsgId::TYPEFACE_INSTALL_FAILED), fam.name.c_str(),
                                          "", "", 2500, true);
                        typefaceFamilies(true);
                        drawTypefaceMenuScreen(ctx, true);
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
        case BTN_BACK:
        case BTN_LEFT:
            ctx.currentState = STATE_APPEARANCE_MENU;
            drawAppearanceScreen(ctx, false);
            break;
        default:
            break;
    }
}

static void openTypefaces(AppContext& ctx) {
    typefaceFamilies(true);
    ctx.selectedTypefaceIndex = 0;
    if (!Reader.usingBuiltinFont()) {
        const auto& fams = typefaceFamilies(false);
        for (size_t i = 0; i < fams.size(); i++)
            if (fams[i].name == Reader.fontFamilyName()) ctx.selectedTypefaceIndex = (int)i + 1;
    }
    ctx.currentState = STATE_TYPEFACE_MENU;
    drawTypefaceMenuScreen(ctx, false);
}

void handleAppearanceEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    int& sel = ctx.selectedAppearanceIndex;
    switch (event.id) {
        case BTN_DOWN:
            sel = (sel + 1) % Appearance::COUNT;
            drawAppearanceScreen(ctx, false);
            break;
        case BTN_UP:
            sel = (sel - 1 + Appearance::COUNT) % Appearance::COUNT;
            drawAppearanceScreen(ctx, false);
            break;
        case BTN_LEFT:
        case BTN_RIGHT:
        case BTN_CONFIRM:
            if (sel == Appearance::TYPEFACE && event.id != BTN_LEFT) {
                openTypefaces(ctx);
            } else if (sel == Appearance::STATUS_BAR && event.id != BTN_LEFT) {
                ctx.selectedStatusBarIndex = 0;
                ctx.currentState = STATE_STATUSBAR_MENU;
                drawStatusBarScreen(ctx, false);
            } else if (sel == Appearance::SIZE && event.id == BTN_CONFIRM) {
                ctx.selectedFontIndex = ctx.config.readerFontSize;
                ctx.currentState = STATE_FONT_MENU;
                drawFontMenuScreen(ctx, false);
            } else if (Appearance::step(ctx.config, sel, event.id == BTN_LEFT ? -1 : 1)) {
                ctx.config.save();
                drawAppearanceScreen(ctx, false);   // (the book turns when the menus are left)
            }
            break;
        case BTN_BACK:
            returnToReading(ctx);   // laid out once, with everything changed here
            break;
        default:
            break;
    }
}

void handleStatusBarEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    int& sel = ctx.selectedStatusBarIndex;
    switch (event.id) {
        case BTN_DOWN:
            sel = (sel + 1) % StatusBarMenu::COUNT;
            drawStatusBarScreen(ctx, false);
            break;
        case BTN_UP:
            sel = (sel - 1 + StatusBarMenu::COUNT) % StatusBarMenu::COUNT;
            drawStatusBarScreen(ctx, false);
            break;
        case BTN_LEFT:
        case BTN_RIGHT:
        case BTN_CONFIRM:
            StatusBarMenu::step(ctx.config, sel, event.id == BTN_LEFT ? -1 : 1);
            ctx.config.save();
            drawStatusBarScreen(ctx, false);
            break;
        case BTN_BACK:
            ctx.currentState = STATE_APPEARANCE_MENU;
            drawAppearanceScreen(ctx, false);
            break;
        default:
            break;
    }
}

void handleBookmarksEvent(AppContext& ctx, const ButtonEvent& event) {
    if (event.action != ACTION_CLICK) return;
    const int n = 1 + Bookmarks.count();
    int& sel = ctx.selectedBookmarkIndex;
    switch (event.id) {
        case BTN_DOWN:
            sel = (sel + 1) % n;
            drawBookmarksScreen(ctx, false);
            break;
        case BTN_UP:
            sel = (sel - 1 + n) % n;
            drawBookmarksScreen(ctx, false);
            break;
        case BTN_CONFIRM:
        case BTN_RIGHT:
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
                    Serial.printf("[Bookmarks] added at %d:%u (%.1f%%)\n", b.spine, (unsigned)b.chars, b.percent * 100.0f);
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
        case BTN_LEFT:
            if (sel > 0 && sel <= Bookmarks.count()) {
                Bookmarks.remove(sel - 1);
                Bookmarks.save();
                if (sel > Bookmarks.count()) sel = Bookmarks.count();
                drawBookmarksScreen(ctx, false);
            }
            break;
        case BTN_BACK:
            ctx.currentState = STATE_READER_MENU;
            drawReaderMenuScreen(ctx, false);
            break;
        default:
            break;
    }
}

}  // namespace ReaderScreen
