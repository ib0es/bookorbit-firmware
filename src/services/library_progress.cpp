#include "library_manager.h"
#include "library_internal.h"
#include "../core/progress_sync.h"
#include "sync_clock.h"
#include "../reader/reader.h"
#include "../convert/sd_convert.h"
#include "../reader/book_reader.h"
#include "../core/ui_port.h"
#include "../net/bookorbit_client.h"
#include "../core/book_formats.h"
#include "../ui/i18n.h"
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include "../storage/catalog_store.h"
#include "../storage/last_book_store.h"

using namespace LibraryInternal;

void LibraryManager::initLastBook() {
    if (LastBookStore::load(lastBook)) {
        ProgressStore& store = ProgressStore::instance();
        if (BookProgressRecord* rec = store.find(lastBook.fileId, lastBook.filePath)) {
            // progress.json is the source of truth for the position.
            applyRecordToLibrary(*rec);
        } else if (ProgressSync::hasProgress(lastBook.progress)) {
            // Migration from firmware without progress.json: keep the old position
            // as "local, never synced" (no base). The next sync pulls the server
            // position if it has one, otherwise uploads this one.
            BookProgressRecord& r = store.getOrCreate(lastBook.fileId, 0, lastBook.filePath, lastBook.title);
            r.state.local.percent = lastBook.progress;
            r.state.local.page = lastBook.page;
            r.state.local.xpath = lastBook.xpath.c_str();
            store.save();
        }
        // Resolve fileId if missing in lastBook
        if (lastBook.fileId <= 0) {
            for (const auto& item : masterBooks) {
                if (item.filePath.equalsIgnoreCase(lastBook.filePath)) {
                    if (item.fileId > 0) {
                        lastBook.fileId = item.fileId;
                        saveLastBook();
                        break;
                    }
                }
            }
        }
    } else if (!masterBooks.empty() || !offlineBooks.empty()) {
        // Fallback to first book on the device (preferring catalog library, then offline).
        const BookItem& b = !masterBooks.empty() ? masterBooks[0] : offlineBooks[0];
        lastBook.filePath = b.filePath;
        lastBook.title = b.title;
        lastBook.author = b.author;
        lastBook.coverPath = b.coverPath;
        lastBook.fileId = b.fileId;
        lastBook.progress = b.progress;
        lastBook.page = b.page;
        lastBook.totalPages = 1;
        saveLastBook();
    }
}


bool LibraryManager::saveLastBook() {
    touch();
    _unsavedMoves = 0;
    bool ok = LastBookStore::save(lastBook);
    ProgressStore::instance().save();
    return ok;
}

// ---------------------------------------------------------------- progress

BookProgressRecord& LibraryManager::currentRecord() {
    int bookId = 0;
    for (const auto& b : masterBooks) {
        if (b.filePath.equalsIgnoreCase(lastBook.filePath)) { bookId = b.bookId; break; }
    }
    return ProgressStore::instance().getOrCreate(lastBook.fileId, bookId, lastBook.filePath, lastBook.title);
}

// withCfi unzips the current chapter (~0.2-0.5 s): only when saving for sync.
static ProgressSync::Position readerPosition(bool withXPath, bool withCfi = false) {
    ProgressSync::Position pos;
    pos.percent = Reader.getProgress();
    pos.page = Reader.getCurrentPage();
    if (withXPath) pos.xpath = Reader.getXPath().c_str();
    if (withCfi) pos.cfi = Reader.getCfi().c_str();
    return pos;
}

void LibraryManager::recordReaderMove(bool withXPath, bool persist) {
    touch();
    if (!Reader.isOpened()) return;
    ProgressSync::Position pos = readerPosition(withXPath);
    lastBook.progress = pos.percent;
    lastBook.page = pos.page;
    lastBook.totalPages = Reader.getTotalPages();
    lastBook.xpath = pos.xpath.c_str();

    BookProgressRecord& rec = currentRecord();
    ProgressSync::recordLocalMove(rec.state, pos, SyncClock::nowUtc());
    ProgressStore::instance().touch();
    if (persist) {
        saveLastBook();
    } else if (_unsavedMoves++ == 0) {
        _firstUnsavedAt = millis();
    }
}

bool LibraryManager::persistIfDue() {
    constexpr int kMaxUnsavedPages = 5;
    constexpr uint32_t kMaxUnsavedMs = 60000;
    if (_unsavedMoves == 0) return false;
    if (_unsavedMoves < kMaxUnsavedPages && millis() - _firstUnsavedAt < kMaxUnsavedMs) return false;
    const uint32_t t0 = millis();
    const int pages = _unsavedMoves;
    saveLastBook();
    Serial.printf("[Library] Position saved while idle (%d page turns) in %lu ms\n", pages, (unsigned long)(millis() - t0));
    return true;
}

void LibraryManager::flushReaderPosition() {
    touch();
    if (!Reader.isOpened()) return;
    ProgressSync::Position pos = readerPosition(true, true);
    lastBook.progress = pos.percent;
    lastBook.page = pos.page;
    lastBook.totalPages = Reader.getTotalPages();
    lastBook.xpath = pos.xpath.c_str();

    // Not a user move: never touches the dirty flag or the change time.
    BookProgressRecord& rec = currentRecord();
    if (ProgressSync::samePlace(rec.state.local, pos)) {
        // Keep the percentage as recorded (it may be the exact server value after
        // a pull; the reader's page start is a little earlier).
        rec.state.local.page = pos.page;
        rec.state.local.xpath = pos.xpath;
        // A CFI from the server is exact; the page-start one is only filled in when missing.
        if (rec.state.local.cfi.empty()) rec.state.local.cfi = pos.cfi;
    } else {
        rec.state.local = pos;
    }
    ProgressStore::instance().touch();
    saveLastBook();
}

void LibraryManager::applyRecordToLibrary(const BookProgressRecord& rec) {
    touch();
    const ProgressSync::Position& pos = rec.state.local;
    if (lastBook.filePath.equalsIgnoreCase(rec.filePath) || (rec.fileId > 0 && lastBook.fileId == rec.fileId)) {
        lastBook.progress = pos.percent;
        lastBook.page = pos.page > 0 ? pos.page : 1;
        lastBook.xpath = pos.xpath.c_str();
    }
    for (auto& b : masterBooks) {
        if (b.filePath.equalsIgnoreCase(rec.filePath) || (rec.fileId > 0 && b.fileId == rec.fileId)) {
            b.progress = pos.percent;
            b.page = pos.page > 0 ? pos.page : 1;
            b.xpath = pos.xpath.c_str();
        }
    }
    for (auto& b : offlineBooks) {
        if (b.filePath.equalsIgnoreCase(rec.filePath)) {
            b.progress = pos.percent;
            b.page = pos.page > 0 ? pos.page : 1;
            b.xpath = pos.xpath.c_str();
        }
    }
}

bool LibraryManager::loadLastBook() {
    return LastBookStore::load(lastBook);
}

