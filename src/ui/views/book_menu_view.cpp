#include "book_menu_view.h"
#include "../../platform/hal/storage.h"
#include "../i18n.h"
#include "../kit/ui_kit.h"
#include "../../core/book_formats.h"

using namespace uikit;

namespace {
enum BookMenuHit : fi::ActionId { ActionBookMenuItem = 1 };
}

int bookMenuItems(const BookItem& book, BookMenuAction out[BOOK_MENU_MAX]) {
    int n = 0;
    const bool onDevice = book.isDownloaded && Storage.exists(book.filePath.c_str());
    const bool onServer = book.fileId > 0 || book.downloadUrl().length() > 0;
    if (onDevice)
        out[n++] = BOOK_MENU_READ;
    else if (book.downloadUrl().length() > 0)
        out[n++] = BOOK_MENU_DOWNLOAD;
    if (book.formats.size() > 1) out[n++] = BOOK_MENU_FORMATS;
    if (onServer && onDevice) out[n++] = BOOK_MENU_SYNC;   // a book only on the server has no position here
    if (onDevice) out[n++] = BOOK_MENU_DELETE;
    if (LibraryManager::isOfflinePath(book.filePath.c_str())) out[n++] = BOOK_MENU_WEB;
    out[n++] = BOOK_MENU_BACK;
    return n;
}

BookItem* bookMenuBook() {
    auto& lib = LibraryManager::getInstance();
    if (lib.selectedBookIndex < 0 || lib.selectedBookIndex >= lib.rowCount()) return nullptr;
    BookItem& b = lib.rowAt(lib.selectedBookIndex);
    return (b.isDirectory || b.formatRow >= 0) ? nullptr : &b;   // format rows open directly
}

void drawBookMenuScreen(AppContext& ctx, bool fullRefresh) {
    const uint32_t t0 = millis();
    BookItem* book = bookMenuBook();
    Pass pass;
    Frame& f = pass.frame();
    clear();
    Rect area = topBar(f, tr(MsgId::BOOK_MENU_TITLE));
    area = keyHints(f, area,
                    KeyLabels{tr(MsgId::KEY_BACK), tr(MsgId::KEY_SELECT), tr(MsgId::KEY_BACK), tr(MsgId::KEY_SELECT)});
    area = insetXY(area, kMargin, 8);
    if (!book) {
        present(fullRefresh);
        return;
    }

    // Book card: title, author, where it is and how far it was read.
    {
        const int16_t sh = lineHeight(FontSmall);
        const int16_t titleH = wrappedHeight(book->title.c_str(), FontTitle, (int16_t)(area.width - 24), 2);
        const bool hasAuthor = book->author.length() > 0;
        Rect st = takeTop(area, (int16_t)(titleH + (hasAuthor ? 2 : 1) * (sh + 2) + 7 + 30));
        frameRect(st, Color::Black);
        Rect in = insetXY(st, 12, 8);
        text(takeTop(in, titleH), book->title.c_str(), FontTitle, Color::Black, TextAlign::Left, 2);
        takeTop(in, 2);
        if (hasAuthor) text(takeTop(in, (int16_t)(sh + 2)), book->author.c_str(), FontSmall);
        const bool onDevice = book->isDownloaded && Storage.exists(book->filePath.c_str());
        char status[112];
        // A book only on the server: where it is, no position (none is asked for).
        if (onDevice)
            snprintf(status, sizeof(status), tr(MsgId::BOOK_MENU_STATUS_FMT), tr(MsgId::BOOK_MENU_ON_DEVICE),
                     book->progress * 100.0f);
        else
            snprintf(status, sizeof(status), "%s", tr(MsgId::BOOK_MENU_IN_CLOUD));
        if (book->formats.size() > 1) {   // which of its formats this is about
            const size_t used = strlen(status);
            snprintf(status + used, sizeof(status) - used, " \xC2\xB7 %s",
                     BookFormats::label((BookFormats::Kind)book->activeKind()));
        }
        text(takeTop(in, (int16_t)(sh + 2)), status, FontSmall);
        takeTop(in, 5);
        if (onDevice) progress(f, takeTop(in, 7), (int32_t)(book->progress * 1000.0f));
    }
    takeTop(area, 8);

    BookMenuAction items[BOOK_MENU_MAX];
    const int n = bookMenuItems(*book, items);
    if (ctx.selectedBookMenuIndex >= n) ctx.selectedBookMenuIndex = n - 1;
    // A landscape screen: tighter cards, then a page at a time.
    const CardList l =
            cardList(area, n, ctx.selectedBookMenuIndex, (int16_t)(lineHeight(FontTitle) + lineHeight(FontSmall) + 22));
    for (int i = l.first; i < n && i < l.first + l.perPage; i++) {
        MsgId title, sub;
        switch (items[i]) {
            case BOOK_MENU_READ:
                title = MsgId::BOOK_MENU_READ;
                sub = MsgId::BOOK_MENU_READ_SUB;
                break;
            case BOOK_MENU_DOWNLOAD:
                title = MsgId::BOOK_MENU_DOWNLOAD;
                sub = MsgId::BOOK_MENU_DOWNLOAD_SUB;
                break;
            case BOOK_MENU_FORMATS:
                title = MsgId::BOOK_MENU_FORMATS;
                sub = MsgId::BOOK_MENU_FORMATS_SUB;
                break;
            case BOOK_MENU_SYNC:
                title = MsgId::BOOK_MENU_SYNC;
                sub = MsgId::BOOK_MENU_SYNC_SUB;
                break;
            case BOOK_MENU_DELETE:
                title = MsgId::BOOK_MENU_DELETE;
                sub = MsgId::BOOK_MENU_DELETE_SUB;
                break;
            case BOOK_MENU_WEB:
                title = MsgId::BOOK_MENU_WEB;
                sub = MsgId::BOOK_MENU_WEB_SUB;
                break;
            default:
                title = MsgId::BOOK_MENU_BACK;
                sub = MsgId::BOOK_MENU_BACK_SUB;
                break;
        }
        card(f, takeTop(area, l.cardH), tr(title), tr(sub), nullptr, i == ctx.selectedBookMenuIndex, ActionBookMenuItem,
             (int16_t)i);
        takeTop(area, l.gap);
    }
    const uint32_t t1 = millis();
    present(fullRefresh);
    Serial.printf("[UI] drawBookMenuScreen: layout %lu ms, display %lu ms\n", (unsigned long)(t1 - t0),
                  (unsigned long)(millis() - t1));
}
