#include "progress_sync.h"

#include <cmath>

namespace ProgressSync {

bool samePlace(const Position& a, const Position& b) {
    return std::fabs(a.percent - b.percent) <= SAME_POSITION_EPSILON;
}

Decision decide(const BookState& s, const Remote& r) {
    const bool localMoved = s.localDirty;
    bool serverMoved;
    if (!r.exists) {
        serverMoved = false;
    } else if (s.hasBase) {
        serverMoved = !samePlace(r.pos, s.base);
    } else {
        // Never synced this book: any real server progress is news.
        serverMoved = hasProgress(r.pos.percent);
    }

    if (!localMoved && !serverMoved) {
        // First contact with a book we have read here but never uploaded.
        if (!s.hasBase && !r.exists && hasProgress(s.local.percent)) {
            return {Action::Push, "server has no record"};
        }
        return {Action::None, "in sync"};
    }
    if (localMoved && !serverMoved) return {Action::Push, "only device moved"};
    if (!localMoved && serverMoved) return {Action::Pull, "only server moved"};

    // Both moved.
    if (samePlace(s.local, r.pos)) return {Action::None, "both moved to the same place"};
    if (s.localChangedAt == 0 || r.updatedAt == 0) {
        return {Action::Push, "conflict, time unknown: device wins"};
    }
    if (r.updatedAt > s.localChangedAt) return {Action::Pull, "conflict: server change is newer"};
    return {Action::Push, "conflict: device change is newer"};
}

void recordLocalMove(BookState& s, const Position& pos, uint32_t nowUtc) {
    s.local = pos;
    s.localDirty = true;
    s.localChangedAt = nowUtc;
}

void applyPull(BookState& s, const Remote& r) {
    s.local = r.pos;
    s.base = r.pos;
    s.hasBase = true;
    s.baseServerUpdatedAt = r.updatedAt;
    s.localDirty = false;
}

void applyPush(BookState& s, uint32_t serverUpdatedAt) {
    s.base = s.local;
    s.hasBase = true;
    s.baseServerUpdatedAt = serverUpdatedAt;
    s.localDirty = false;
}

void applyConverged(BookState& s, const Remote& r) {
    if (r.exists) {
        s.base = r.pos;
        s.baseServerUpdatedAt = r.updatedAt;
        s.hasBase = true;
    } else if (!s.hasBase) {
        s.base = s.local;
        s.hasBase = true;
    }
    s.localDirty = false;
}

const char* actionName(Action a) {
    switch (a) {
        case Action::None: return "none";
        case Action::Push: return "push";
        case Action::Pull: return "pull";
    }
    return "?";
}

}  // namespace ProgressSync
