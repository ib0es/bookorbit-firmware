#pragma once

// The reader on FreeInkBook (freeink-sdk/libs/book): EPUB (and TXT) laid out by the
// engine, chapter by chapter, into page caches on the card; a page turn is a
// cache read and a glyph blit.
//
// Positions are (spine item, character offset in the chapter's text), which
// survive any change of font size or page geometry. Page numbers are
// whole-book: exact for chapters already laid out, estimated (by bytes) for the
// others, so the total can move a little as chapters get laid out.
//
// Memory (ESP32-C3 without PSRAM): laying out a chapter needs two transient
// buffers (layout ~56 KB, parse ~48 KB) taken right before and freed right
// after; while reading only the container index (a few KB) and the current
// chapter's page index (8 bytes per page) stay allocated. A layout that does
// not fit is retried at the next page turn (e.g. once Wi-Fi is off).
//
// The public API mirrors SimpleReader, which this class replaces.

#include <Arduino.h>
#include <vector>

#include <BookCatalog.h>
#include <cache/PageCache.h>
#include <layout/ChapterLayout.h>
#include <render/TtfFont.h>
#include <text/Hyphenator.h>

#include "chapter_item.h"
#include "epub_cfi.h"
#include "engine/bitmap_book_font.h"
#include "engine/sd_book_storage.h"

class BookReader {
public:
    BookReader();
    ~BookReader();

    bool openBook(const String& filePath);
    void closeBook();

    bool isOpened() const { return _opened; }
    // The last openBook() failed for lack of memory (not a broken or DRM book).
    bool lastFailedForMemory() const { return _noMemory; }
    String getTitle() const { return _title; }
    String getOriginalFilePath() const { return _path; }
    // 0..1: whole chapters by their (exactly known) size, the current one by characters.
    float getProgress() const;
    int getCurrentPage() const;         // 1-based, whole book
    int getTotalPages() const;

    // Table of contents
    bool hasChapters() const { return !_toc.empty(); }
    int getChapterCount() const { return (int)_toc.size(); }
    const std::vector<ChapterItem>& getChapters();
    int getCurrentChapterIndex() const;
    String getCurrentChapterTitle() const;
    bool nextChapter();
    bool prevChapter();
    bool goToChapter(int chapterIndex);

    // Pages
    bool nextPage();
    bool prevPage();
    bool setPage(int page);
    bool setPageByProgress(float percent);
    // KOReader-style "/body/DocFragment[N]/...": the chapter only (N = spine item + 1).
    bool setPageByXPath(const String& xpath, float fallbackPercent = 0.0f);
    String getXPath() const;

    // EPUB CFI of the Book Orbit web reader. A CFI that lands more than 10% away
    // from `fallbackPercent` is taken as stale (returns false, does not move).
    bool setPageByCfi(const String& cfi, float fallbackPercent);
    String getCfi();
    bool goToPosition(const String& cfi, const String& xpath, float percent, int page);
    void renderCurrentPage();
    int renderFailures() const { return _renderFailures; }   // pages that could not be drawn (tests, logs)

    int getFontSize() const { return _fontSize; }
    bool setFontSize(int size);
    static const char* getFontSizeName(int size);

    // Most memory the reader's transient buffers (layout, parse, inflate, page
    // record) held at once since the last reset. The X4's biggest free block
    // after boot is ~96 KB: they must stay under it.
    static size_t peakTransientBytes(bool reset = false);

    // Copies an EPUB's cover image (as the package declares it) to `outPath`. Builds
    // the book's index if it has none (kept for opening the book). `noMemory` is set
    // when it failed for lack of memory (worth trying again later).
    static bool extractCover(const String& epubPath, const String& outPath, bool* noMemory = nullptr);

    // The book's layout cache directory on the card ("/cache/<file name>.fib").
    static String cacheDirFor(const String& bookPath);

    // Position as the engine keeps it.
    struct Position {
        int spine = 0;
        uint32_t chars = 0;
    };
    Position position() const;
    bool goTo(const Position& pos);

    // Lays out every chapter not laid out yet (exact page numbers from then on).
    // `maxChapters` bounds the work of one call; returns true when none is left.
    bool layoutRemaining(int maxChapters = 1 << 30);

    // Background layout of the chapters ahead, so that turning into the next
    // chapter does not wait for it. tickBackground() does one slice (a few
    // pages, or ~8 KB of unpacking: tens of milliseconds) and is called from
    // the main loop while the reader sits idle. The job holds ~64 KB; anything
    // in the foreground that needs that memory (another chapter, a big page,
    // an image, Wi-Fi) stops it first, and a chapter the reader turns to while
    // it is being laid out is finished instead of started over.
    bool tickBackground();
    void stopBackground();
    bool backgroundActive() const { return _bg != nullptr; }
    static constexpr int kLookahead = 2;   // chapters ahead of the current one
    // Pages of a laid-out spine item, -1 if it is not laid out (tests, logs).
    int chapterPages(int spine) const {
        return spine >= 0 && spine < (int)_spines.size() ? (int)_spines[spine].pages : -1;
    }
    int spineCount() const { return (int)_spines.size(); }

private:
    struct Prelayout;
    Prelayout* _bg = nullptr;
    int _bgNothingFor = -1;   // the current chapter for which nothing was left to lay out ahead
    int nextToPrelayout() const;
    bool startBackground(int spine);
    bool stepBackground();                    // false: the job ended (done or failed)
    bool finishBackground();                  // runs the job on the current chapter to its end
    // The chapter on screen while it is still being laid out: its pages come from
    // the job's writer until the job ends, then from the cache file as usual.
    bool building() const;
    void makeRoom();                          // frees the job's memory the cheaper way (drop or finish)
    bool buildIncrementally(int spine);       // starts (or adopts) the job, waits for the first page
    void buildUntilPages(uint32_t count);     // waits until `count` pages exist (or the chapter ends)
    void buildUntilChar(uint32_t chars);      // until the page holding `chars` is complete
    void buildUntilAnchor(uint32_t idHash);
    uint32_t curCount() const;
    uint32_t curCharStart(uint32_t page) const;
    uint32_t curPageForChar(uint32_t chars) const;
    bool curCharForAnchor(uint32_t idHash, uint32_t* chars) const;
    uint32_t curTotalChars() const;           // estimated while building

    struct Spine {
        uint32_t bytes = 0;     // uncompressed size (estimates for chapters not laid out)
        int32_t pages = -1;     // -1: not laid out yet in this generation
        uint32_t chars = 0;
        bool bgFailed = false;  // the background job could not lay it out: left to the foreground
    };
    // Where to go once the chapter is laid out.
    enum class Want : uint8_t { None, Char, Page, LastPage, Anchor, Fraction };

    bool openContainer();
    void setupLayout();
    bool loadChapter(int spine);              // laid out (if needed) and its page index open
    bool layoutChapter(int spine);
    bool chapterSource(int spine, freeink::book::ZipEntry& entry, freeink::book::BookSource*& source,
                       SdBookSource& unpacked);
    void commitLaidOut(int spine, uint32_t pages, uint32_t chars);
    bool walkChapter(int spine, EpubCfi::Walker& walker);
    bool resolve();                           // applies _want; false if the chapter is not available
    void settle() const { if (_want != Want::None) const_cast<BookReader*>(this)->resolve(); }
    void loadSpineTable();
    void saveSpineTable();
    void buildToc();
    float pagesPerByte() const;
    int pagesOf(int spine) const;             // exact or estimated
    uint32_t charsOf(int spine) const;
    int globalPage(int spine, int page) const;
    bool chapterCharToCfi(int spine, uint32_t chars, String& out);
    bool cfiToChapterChar(const String& cfi, int& spine, uint32_t& chars);
    void drawStatusBar();
    void releaseIndex();

    bool _opened = false;
    bool _noMemory = false;
    bool _isTxt = false;
    String _path;
    String _title;
    int _fontSize;

    SdBookSource _src;
    SdBookSource _chapterSrc;   // an unpacked chapter while it is laid out or walked
    SdCacheStorage _cache;
    freeink::book::BookCatalog _catalog;
    uint8_t* _bookMem = nullptr;
    freeink::book::Arena _bookArena;

    BitmapBookFont _font;
    freeink::book::FontChain _fonts;
    freeink::book::Hyphenator _hyphEn;
    freeink::book::Hyphenator _hyphRu;
    // Picks the patterns by the book's language ("ru", "ru-RU", "rus", "en"...); a
    // plain-text file has none, so its first kilobytes are looked at instead.
    const char* bookLanguage();
    const freeink::book::Hyphenator* hyphenatorFor(const char* language) const;
    freeink::book::LayoutParams _params;
    uint32_t _gen = 0;

    std::vector<Spine> _spines;
    std::vector<ChapterItem> _toc;
    std::vector<uint32_t> _tocFragment;   // id hash of the TOC target, 0 = chapter start

    // Current chapter and page.
    int _spine = 0;
    int _page = 0;
    int _loadedSpine = -1;
    freeink::book::PageCacheReader _pages;
    uint8_t* _indexMem = nullptr;
    size_t _indexCap = 0;
    freeink::book::Arena _indexArena;

    Want _want = Want::None;
    uint32_t _wantValue = 0;
    float _wantFraction = 0.0f;   // Want::Fraction: share of the chapter's characters

    // getCfi() is a chapter walk: cached for the page it was made for.
    int _cfiSpine = -1;
    uint32_t _cfiChars = UINT32_MAX;
    String _cfi;
    int _renderedPages = 0;
    int _renderFailures = 0;
};
