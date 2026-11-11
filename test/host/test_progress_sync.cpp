// Host unit tests for the progress sync core and time parsing.  Run: test/host/run.sh
#include "core/progress_sync.h"
#include "core/time_utils.h"

#include <cstdio>
#include <string>

using namespace ProgressSync;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Position P(float pct, int page = 0, const char* xp = "") { Position p; p.percent = pct; p.page = page; p.xpath = xp; return p; }
static Remote R(float pct, uint32_t upd, const char* xp = "") { Remote r; r.exists = true; r.pos = P(pct, 0, xp); r.updatedAt = upd; return r; }
static Remote NoRemote() { return Remote(); }

// A book that was synced at `basePct` and not touched since.
static BookState synced(float basePct, uint32_t baseUpd = 1000) {
    BookState s;
    s.local = P(basePct);
    s.base = P(basePct);
    s.hasBase = true;
    s.baseServerUpdatedAt = baseUpd;
    return s;
}

static void expect(const char* name, const BookState& s, const Remote& r, Action want) {
    g_checks++;
    Decision d = decide(s, r);
    if (d.action != want) {
        g_failed++;
        printf("  FAIL %-62s want %s, got %s (%s)\n", name, actionName(want), actionName(d.action), d.reason);
    } else {
        printf("  ok   %-62s %s (%s)\n", name, actionName(d.action), d.reason);
    }
}

static void testDecide() {
    printf("decide: nobody / one side moved\n");
    expect("synced, server unchanged", synced(0.30f), R(0.30f, 1000), Action::None);
    expect("synced, web reader only bumped updatedAt (same place)", synced(0.30f), R(0.302f, 5000), Action::None);
    expect("server moved forward", synced(0.30f), R(0.60f, 2000), Action::Pull);
    expect("server moved BACKWARD (user's bug report)", synced(0.60f), R(0.20f, 2000), Action::Pull);
    expect("server moved with stale timestamp (older than base)", synced(0.30f, 5000), R(0.60f, 100), Action::Pull);
    {
        BookState s = synced(0.30f);
        recordLocalMove(s, P(0.35f), 3000);
        expect("device read further, server unchanged", s, R(0.30f, 1000), Action::Push);
        expect("device read, server only bumped updatedAt", s, R(0.30f, 9000), Action::Push);
    }

    printf("decide: both moved (last change wins)\n");
    {
        BookState s = synced(0.30f);
        recordLocalMove(s, P(0.35f), 3000);
        expect("server changed later -> pull", s, R(0.20f, 4000), Action::Pull);
        expect("device changed later -> push", s, R(0.60f, 2000), Action::Push);
        expect("both ended at the same place -> none", s, R(0.352f, 4000), Action::None);
        expect("server time unknown -> device wins", s, R(0.60f, 0), Action::Push);
    }
    {
        BookState s = synced(0.30f);
        recordLocalMove(s, P(0.35f), 0);   // device clock was not set
        expect("device time unknown -> device wins", s, R(0.60f, 4000), Action::Push);
    }

    printf("decide: first contact (no base)\n");
    {
        BookState s;  // never opened here
        expect("fresh book, server has progress -> pull", s, R(0.40f, 1000), Action::Pull);
        expect("fresh book, server ~0 -> none", s, R(0.001f, 1000), Action::None);
        expect("fresh book, no server record -> none", s, NoRemote(), Action::None);
    }
    {
        BookState s;
        recordLocalMove(s, P(0.25f), 3000);
        expect("read offline, server has no record -> push", s, NoRemote(), Action::Push);
        expect("read offline, server older progress -> push", s, R(0.10f, 1000), Action::Push);
        expect("read offline, server newer progress -> pull", s, R(0.10f, 5000), Action::Pull);
    }
    {
        BookState s;  // local progress loaded from an old version, not dirty, no base
        s.local = P(0.25f);
        expect("legacy local progress, server has no record -> push", s, NoRemote(), Action::Push);
    }
}

static void testTransitions() {
    printf("transitions\n");
    BookState s = synced(0.30f);
    recordLocalMove(s, P(0.35f, 140, "/x"), 3000);
    CHECK(s.localDirty && s.localChangedAt == 3000);

    applyPush(s, 3100);
    CHECK(!s.localDirty && s.hasBase && samePlace(s.base, P(0.35f)) && s.baseServerUpdatedAt == 3100);
    CHECK(decide(s, R(0.35f, 3100)).action == Action::None);

    Remote r = R(0.10f, 5000, "/y");
    applyPull(s, r);
    CHECK(!s.localDirty && samePlace(s.local, P(0.10f)) && samePlace(s.base, P(0.10f)) && s.local.xpath == "/y");
    CHECK(decide(s, r).action == Action::None);

    // The user's scenario end to end: pull, server edited, pull again.
    BookState u;
    Remote first = R(0.30f, 1000);
    CHECK(decide(u, first).action == Action::Pull); applyPull(u, first);
    Remote edited = R(0.60f, 2000);
    CHECK(decide(u, edited).action == Action::Pull); applyPull(u, edited);
    CHECK(samePlace(u.local, P(0.60f)));
    Remote back = R(0.20f, 3000);
    CHECK(decide(u, back).action == Action::Pull); applyPull(u, back);
    CHECK(samePlace(u.local, P(0.20f)));

    BookState c;
    recordLocalMove(c, P(0.4f), 10);
    applyConverged(c, R(0.401f, 20));
    CHECK(!c.localDirty && c.hasBase && c.baseServerUpdatedAt == 20);
}

static void testTime() {
    printf("time parsing\n");
    using namespace TimeUtils;
    CHECK(utcFromCivil(1970, 1, 1, 0, 0, 0) == 0);
    CHECK(utcFromCivil(2000, 3, 1, 0, 0, 0) == 951868800);
    CHECK(parseIso8601("2026-11-07T10:15:30Z") == 1794046530u);
    CHECK(parseIso8601("2026-11-07T10:15:30.123Z") == 1794046530u);
    CHECK(parseIso8601("2026-11-07T13:15:30+03:00") == 1794046530u);
    CHECK(parseIso8601("2026-11-07T05:15:30-0500") == 1794046530u);
    CHECK(parseIso8601("2026-11-07 10:15:30") == 1794046530u);
    CHECK(parseIso8601("2026-11-07T10:15Z") == 1794046500u);
    CHECK(parseIso8601("2024-02-29T00:00:00Z") == 1709164800u);
    CHECK(parseIso8601("") == 0 && parseIso8601(nullptr) == 0);
    CHECK(parseIso8601("garbage") == 0);
    CHECK(parseIso8601("2026-13-07T10:15:30Z") == 0);
    CHECK(parseIso8601("2026-11-07T10:15:30Zjunk") == 0);
    CHECK(parseHttpDate("Sat, 07 Nov 2026 10:15:30 GMT") == 1794046530u);
    CHECK(parseHttpDate("Sat, 7 Nov 2026 10:15:30 GMT") == 1794046530u);
    CHECK(parseHttpDate("Thu, 01 Jan 1970 00:00:01 GMT") == 1u);
    CHECK(parseHttpDate("nonsense") == 0);
}

int main() {
    testDecide();
    testTransitions();
    testTime();
    printf("%s: %d/%d checks passed\n", g_failed ? "FAILED" : "OK", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
