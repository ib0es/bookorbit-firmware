#include <algorithm>
#include <vector>
// Reader position invariants on real books (BookReader, every chapter laid out), for every page:
//   setPageByProgress(getProgress()) and, for EPUBs, setPageByCfi(getCfi(), getProgress()) keep
//   the page; setPageByXPath(getXPath(), ...) keeps the chapter (the xpath names only that).
// Plus a CFI in the Book Orbit web reader format for "01. Frankenstein (1818).epub"
// (the start of "Chapter 10") must open the page where that chapter starts.
//   reader_roundtrip <sd root> <book path>...
#include <cstdio>
#include <string>
#include "Arduino.h"
#include "InputManager.h"
#include "EInkDisplay.h"
#include "SDCardManager.h"
#include "platform/hal/storage.h"
#include "platform/hal/display.h"
#include "ui/font.h"
#include "reader/reader.h"

bool InputManager::s_simState[InputManager::BTN_COUNT] = {false};
bool InputManager::s_oneShotClicks[InputManager::BTN_COUNT] = {false};
freeink::DisplayUpdateCallback freeink::FreeInkDisplay::s_callback = nullptr;
void simPumpEvents() {}
void simInputPolled() {}

// Background layout from a clean cache: the chapters ahead of the open one get
// laid out in short slices, to the same pages as the foreground gives them, and a
// chapter turned to while its job runs is finished rather than started over.
static int backgroundLayout(const char* path) {
    int fail = 0;
    Storage.removeDir(BookReader::cacheDirFor(path).c_str());
    BookReader::peakTransientBytes(true);
    if (!Reader.openBook(path)) {
        printf("  bg: open failed\n");
        return 1;
    }
    Reader.setPage(1);
    Reader.renderCurrentPage();
    const int from = Reader.position().spine;
    int slices = 0;
    uint32_t longest = 0;
    for (;;) {
        const uint32_t t0 = millis();
        if (!Reader.tickBackground()) break;
        longest = std::max(longest, (uint32_t)(millis() - t0));
        if (++slices > 100000) {
            printf("  bg: never ends\n");
            fail++;
            break;
        }
    }
    std::vector<int> bgPages;
    for (int s = from + 1; s <= from + BookReader::kLookahead && s < Reader.spineCount(); s++) {
        bgPages.push_back(Reader.chapterPages(s));
        if (Reader.chapterPages(s) < 0) {
            printf("  bg: chapter %d not laid out\n", s);
            fail++;
        }
    }
    const size_t peak = BookReader::peakTransientBytes();
    // Next to the job (64 KB) the page being drawn must still fit.
    Reader.setPage(2);
    Reader.renderCurrentPage();
    Reader.closeBook();

    // The same chapters laid out in the foreground.
    Storage.removeDir(BookReader::cacheDirFor(path).c_str());
    Reader.openBook(path);
    Reader.layoutRemaining();
    for (size_t i = 0; i < bgPages.size(); i++) {
        const int s = from + 1 + (int)i;
        if (Reader.chapterPages(s) != bgPages[i]) {
            printf("  bg: chapter %d has %d pages, %d in the foreground\n", s, bgPages[i], Reader.chapterPages(s));
            fail++;
        }
    }
    Reader.closeBook();

    // Turning into a chapter whose job is half done finishes it.
    Storage.removeDir(BookReader::cacheDirFor(path).c_str());
    Reader.openBook(path);
    Reader.setPage(1);
    Reader.renderCurrentPage();
    for (int i = 0; i < 3; i++) Reader.tickBackground();
    const bool running = Reader.backgroundActive();
    const int drawFails = Reader.renderFailures();
    Reader.nextChapter();
    Reader.renderCurrentPage();
    if (Reader.renderFailures() != drawFails || Reader.position().spine <= from) {
        printf("  bg: the next chapter did not open\n");
        fail++;
    }
    Reader.closeBook();
    printf("  background: %d slices, longest %u ms, chapters %d..%d, transient peak %u KB, job %s at the turn%s\n",
           slices, (unsigned)longest, from + 1, from + (int)bgPages.size(), (unsigned)(peak / 1024),
           running ? "running" : "idle", fail ? " FAILED" : "");
    if (peak > 80 * 1024) {
        printf("  bg: transient peak over 80 KB\n");
        fail++;
    }
    return fail;
}

// A chapter shown while it is laid out: from a clean cache, turning pages one by
// one (no idle time, so only what each turn needs is made) must give the very same
// pages as the book laid out in full; stopping the job mid-chapter (leaving for the
// library, Wi-Fi) must come back to the same text.
static int incrementalLayout(const char* path) {
    int fail = 0;
    Storage.removeDir(BookReader::cacheDirFor(path).c_str());
    BookReader::peakTransientBytes(true);
    if (!Reader.openBook(path)) {
        printf("  incremental: open failed\n");
        return 1;
    }
    Reader.setPage(1);
    Reader.renderCurrentPage();
    bool built = false;   // some chapter was shown while it was laid out
    std::vector<std::pair<int, uint32_t>> seen;
    seen.push_back({Reader.position().spine, Reader.position().chars});
    int stops = 0, stopFail = 0;
    while (Reader.nextPage()) {
        built = built || Reader.backgroundActive();
        const BookReader::Position here = Reader.position();
        seen.push_back({here.spine, here.chars});
        if (seen.size() % 37 == 0 && Reader.backgroundActive()) {
            const int pageBefore = Reader.getCurrentPage();
            Reader.stopBackground();
            stops++;
            // where the reader is, known without laying the chapter out again (a sync, sleep)
            if (Reader.getCurrentPage() != pageBefore || Reader.backgroundActive()) {
                if (stopFail++ < 3)
                    printf("  incremental: after a stop, page %d (was %d), %s\n", Reader.getCurrentPage(), pageBefore,
                           Reader.backgroundActive() ? "laid out again" : "not laid out");
            }
            if (Reader.position().spine != here.spine || Reader.position().chars != here.chars) {
                if (stopFail++ < 3)
                    printf("  incremental: after a stop at %d:%u, %d:%u\n", here.spine, (unsigned)here.chars,
                           Reader.position().spine, (unsigned)Reader.position().chars);
            }
        }
        if (seen.size() > 100000) break;
    }
    const size_t peak = BookReader::peakTransientBytes();
    const int failsDraw = Reader.renderFailures();
    Reader.closeBook();
    Reader.openBook(path);
    Reader.layoutRemaining();
    const int n = Reader.getTotalPages();
    int differ = 0;
    if ((int)seen.size() != n) {
        printf("  incremental: %d pages turned, %d in the book\n", (int)seen.size(), n);
        fail++;
    }
    for (int p = 1; p <= n && p <= (int)seen.size(); p++) {
        Reader.setPage(p);
        const BookReader::Position at = Reader.position();
        if (at.spine != seen[p - 1].first || at.chars != seen[p - 1].second) {
            if (differ++ < 3)
                printf("  incremental: page %d at %d:%u, laid out in full %d:%u\n", p, seen[p - 1].first,
                       (unsigned)seen[p - 1].second, at.spine, (unsigned)at.chars);
        }
    }
    Reader.closeBook();
    fail += differ + stopFail + (built ? 0 : 1) + (failsDraw ? 1 : 0) + (peak > 96 * 1024 ? 1 : 0);
    printf("  incremental: first chapter %s, %d pages turned, %d differ, %d stops, transient peak %u KB%s\n",
           built ? "shown while laid out" : "NOT INCREMENTAL", (int)seen.size(), differ, stops, (unsigned)(peak / 1024),
           fail ? " FAILED" : "");
    return fail;
}

// The cover image the package declares, copied out byte for byte (a JPEG here).
static int coverCheck(const char* path) {
    const char* out = "/covers/_roundtrip.jpg";
    Storage.remove(out);
    const bool ok = BookReader::extractCover(path, out);
    uint8_t magic[2] = {0, 0};
    size_t size = 0;
    if (ok) {
        FsFile f = Storage.open(out, O_RDONLY);
        size = f.size();
        f.read(magic, 2);
        f.close();
    }
    Storage.remove(out);
    const bool good =
            ok && size > 1000 && ((magic[0] == 0xFF && magic[1] == 0xD8) || (magic[0] == 'B' && magic[1] == 'M'));
    printf("  cover: %s (%u bytes)\n", good ? "ok" : "FAILED", (unsigned)size);
    return good ? 0 : 1;
}

int main(int argc, char** argv) {
    SDCardManager::getInstance().setRoot(argv[1]);
    Display.begin();
    Font.begin(Display);
    Storage.begin();
    int totalFail = 0;
    for (int a = 2; a < argc; a++) {
        if (String(argv[a]).endsWith(".epub"))
            totalFail += backgroundLayout(argv[a]) + incrementalLayout(argv[a]) + coverCheck(argv[a]);
        BookReader::peakTransientBytes(true);
        if (!Reader.openBook(argv[a])) {
            printf("open failed %s\n", argv[a]);
            totalFail++;
            continue;
        }
        Reader.layoutRemaining();   // exact page numbers
        const int n = Reader.getTotalPages();
        int failP = 0, failX = 0, failC = 0, checked = 0, shared = 0;
        // Every page must draw (its record fits the render buffer, the chapter is readable).
        const int drawFailsBefore = Reader.renderFailures();
        const char* drawLimit = std::getenv("SIM_DRAW_LARGEST_HEAP_BLOCK");
        const char* oldLimit = std::getenv("SIM_LARGEST_HEAP_BLOCK");
        const std::string savedLimit = oldLimit ? oldLimit : "";
        if (drawLimit) setenv("SIM_LARGEST_HEAP_BLOCK", drawLimit, 1);
        for (int p = 1; p <= n; p++) {
            Reader.setPage(p);
            Reader.renderCurrentPage();
            if (std::getenv("SIM_READER_FRAME_HASHES")) {
                uint64_t hash = 14695981039346656037ULL;
                const uint8_t* frame = Display.getBuffer();
                for (size_t i = 0; i < EPD_BUFFER_SIZE; ++i) hash = (hash ^ frame[i]) * 1099511628211ULL;
                printf("[FrameHash] %d %016llx\n", p, (unsigned long long)hash);
            }
        }
        if (drawLimit) {
            if (savedLimit.empty())
                unsetenv("SIM_LARGEST_HEAP_BLOCK");
            else
                setenv("SIM_LARGEST_HEAP_BLOCK", savedLimit.c_str(), 1);
        }
        const int failD = Reader.renderFailures() - drawFailsBefore;
        const bool epub = String(argv[a]).endsWith(".epub");
        for (int p = 1; p <= n; p++) {
            Reader.setPage(p);
            const BookReader::Position here = Reader.position();
            // Pages that start at the same character (image pages) cannot be told apart by position.
            Reader.setPage(p + 1);
            const bool ambiguous =
                    p < n && Reader.position().spine == here.spine && Reader.position().chars == here.chars;
            if (ambiguous) {
                shared++;
                continue;
            }
            Reader.setPage(p - 1);
            const float prevPct = p > 1 ? Reader.getProgress() : -1.0f;
            Reader.setPage(p);
            const float pct = Reader.getProgress();
            const String x = Reader.getXPath();
            Reader.setPageByProgress(pct);
            // A page without characters before it in its chapter (the cover, a chapter's first page
            // after an empty one) can share its percent with the page before.
            if (Reader.getCurrentPage() != p && pct != prevPct) {
                if (failP++ < 3) printf("  pct  p%d -> p%d (%.6f)\n", p, Reader.getCurrentPage(), pct);
            }
            Reader.setPage(1);
            Reader.setPageByXPath(x, pct);
            if (Reader.getXPath() != x) {
                if (failX++ < 3) printf("  xpath p%d -> %s (%s)\n", p, Reader.getXPath().c_str(), x.c_str());
            }
            if (epub) {
                Reader.setPage(p);
                const String c = Reader.getCfi();
                Reader.setPage(1);
                if (c.length() == 0 || !Reader.setPageByCfi(c, pct) || Reader.getCurrentPage() != p) {
                    if (failC++ < 3) printf("  cfi  p%d -> p%d (%s)\n", p, Reader.getCurrentPage(), c.c_str());
                }
            }
            checked++;
        }
        if (String(argv[a]).endsWith("01. Frankenstein (1818).epub")) {
            // Web-reader style position: epubcfi(/6/34!/4/2[pgepubid00016]/2/3:0) = "Chapter 10" heading.
            Reader.setPage(1);
            const bool ok = Reader.setPageByCfi("epubcfi(/6/34!/4/2[pgepubid00016]/2/3:0)", 0.372f);
            const int page = Reader.getCurrentPage();
            const String here = Reader.getXPath();
            Reader.setPage(page - 1);
            const String before = Reader.getXPath();
            const bool chapterStart = here == "/body/DocFragment[17]" && before == "/body/DocFragment[16]";
            printf("  web CFI 'Chapter 10' -> page %d (%s)%s\n", page, here.c_str(),
                   ok && chapterStart ? "" : "  WRONG");
            if (!ok || !chapterStart) failC++;
        }
        // Transient buffers live at once: under the X4's biggest free block after boot
        // (96 KB measured, Nov 8), with room to spare for the page index and the heap's own overhead.
        const size_t peak = BookReader::peakTransientBytes(true);
        const int failM = peak > 80 * 1024 ? 1 : 0;
        printf("  transient memory peak %u KB%s\n", (unsigned)(peak / 1024), failM ? "  TOO MUCH" : "");
        totalFail += failM;
        printf("%-40s pages=%d checked=%d (image pages sharing a position: %d)  draw-fail=%d  percent-fail=%d  xpath-fail=%d  cfi-fail=%d\n",
               argv[a], n, checked, shared, failD, failP, failX, failC);
        totalFail += failD + failP + failX + failC;
        Reader.closeBook();
    }
    printf("%s\n", totalFail ? "ROUNDTRIP FAILED" : "ROUNDTRIP OK");
    return totalFail ? 1 : 0;
}
