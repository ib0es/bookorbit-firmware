#pragma once

// Reading-progress synchronisation core (pure logic, host-tested:
// test/host/test_progress_sync.cpp). No Arduino, network or storage here.
//
// Model: three-way merge per book.
//   local  - where this device is in the book;
//   base   - the position device and server last agreed on (after a pull or a
//            successful push);
//   remote - what the server reports now.
//
// The server counts as "moved" only if its POSITION differs from base. Its
// updatedAt alone is not trusted for that: the Book Orbit web reader bumps
// updatedAt just by opening a book. The device counts as "moved" when the user
// turned pages since the last agreement (localDirty).
//
//   nobody moved        -> nothing to do
//   only device moved   -> push
//   only server moved   -> pull
//   both moved          -> same place? converge. Otherwise the most recent change
//                          wins (local change time vs server updatedAt). When either
//                          time is unknown (device clock not set yet), the device
//                          wins: it is the one in the user's hands.

// Where the reader lands: the Book Orbit web reader's percentage uses its own
// scale (0.2-0.5% off the text, i.e. several pages), so the exact place travels as
// an EPUB CFI (Position::cfi, "text.cfi" on the server). The reader opens a CFI
// first, checks it against the percentage (a stale CFI is ignored), then falls
// back to its own xpath and finally to the percentage. Each upload replaces the
// whole server position, so the CFI is always sent along.

#include <cstdint>
#include <string>

namespace ProgressSync {

// Positions closer than this (fraction of the book, 0.5%) are "the same place".
// Absorbs rounding between readers that compute percentages differently.
constexpr float SAME_POSITION_EPSILON = 0.005f;

inline bool hasProgress(float percent) { return percent > SAME_POSITION_EPSILON; }

struct Position {
    float percent = 0.0f;   // 0..1
    int page = 0;           // reader page, informational (0 = unknown)
    std::string xpath;      // KOReader-style xpath, may be empty
    std::string cfi;        // EPUB CFI (Book Orbit web reader), may be empty
};

bool samePlace(const Position& a, const Position& b);

// Per-book sync state, persisted on the SD card (ProgressStore).
struct BookState {
    Position local;
    bool localDirty = false;        // pages turned since the last agreement
    uint32_t localChangedAt = 0;    // UTC seconds of the last local move, 0 = unknown
    bool hasBase = false;
    Position base;
    uint32_t baseServerUpdatedAt = 0;  // server updatedAt of the base, informational
};

struct Remote {
    bool exists = false;            // server has a progress record
    Position pos;
    uint32_t updatedAt = 0;         // UTC seconds, 0 = unknown
};

enum class Action { None, Push, Pull };

struct Decision {
    Action action = Action::None;
    const char* reason = "";
};

Decision decide(const BookState& state, const Remote& remote);

// --- state transitions (call after the corresponding step succeeded) ---------

// The user moved to `pos` on this device. `nowUtc` = 0 if the clock is unknown.
void recordLocalMove(BookState& state, const Position& pos, uint32_t nowUtc);

// The server position was taken over: local = base = remote.
void applyPull(BookState& state, const Remote& remote);

// The local position was uploaded: base = local.
void applyPush(BookState& state, uint32_t serverUpdatedAt);

// Nothing was transferred but both sides are at the same place: base = remote
// (or local when the server has no record), dirty flag cleared.
void applyConverged(BookState& state, const Remote& remote);

const char* actionName(Action a);

}  // namespace ProgressSync
