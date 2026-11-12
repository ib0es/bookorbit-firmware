#pragma once

// What the parts of SyncService share (A8: sync_service.cpp was split into
// the common part, one book's progress, downloads, the full sync and the
// background upload). Internal to src/services/sync_*.cpp.

#include <vector>
#include "sync_service.h"
#include "library_manager.h"
#include "../core/progress_sync.h"
#include "../storage/progress_store.h"

namespace SyncInternal {

using ProgressSync::Action;

uint32_t tokenHash(const String& s);

struct KeepToken {
    AppContext& ctx;
    const uint32_t before;
    explicit KeepToken(AppContext& c) : ctx(c), before(tokenHash(c.config.authToken)) {}
    ~KeepToken() {
        if (ctx.config.authToken.length() > 0 && tokenHash(ctx.config.authToken) != before) ctx.config.save();
    }
};

// Logs in if there is no token yet; persists a fresh token.
void ensureToken(AppContext& ctx);

// A full sync works through every book: the files are written once at its end
// (not two JSON files per book), and the saved catalog is read once for all.
extern bool s_batch;

struct SyncBatch {
    std::vector<BookMetadata> catalog;
    bool loaded = false;
    SyncBatch() { s_batch = true; }
    ~SyncBatch() {
        s_batch = false;
        LibraryManager::getInstance().saveLastBook();   // last_book.json + progress.json
    }
};
extern SyncBatch* s_batchState;

const std::vector<BookMetadata>* savedCatalog(std::vector<BookMetadata>& local);
void resolveServerIds(BookProgressRecord& rec);
bool cfiApplies(const BookProgressRecord& rec);
bool isOpenInReader(const BookProgressRecord& rec);
void syncBookmarks(AppContext& ctx, const BookProgressRecord& rec);

}  // namespace SyncInternal
