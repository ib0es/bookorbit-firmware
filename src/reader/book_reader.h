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
#include <render/PageRenderer.h>
#include <render/TtfFont.h>
#include <text/Hyphenator.h>

#include "config.h"
#include "chapter_item.h"
#include "epub_cfi.h"
#include "cfi_map.h"
#include "engine/bitmap_book_font.h"
#include "engine/ttf_book_font.h"
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
    // Why the last openBook() failed to convert the book (FB2/MOBI/AZW3), if it did.
    uint8_t lastConvertProblem() const { return _convertProblem; }
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
    // The same for any place of the open book (bookmarks): a CFI of (spine, chars),
    // the place a CFI points at (false if not in this book), the percent there.
    String cfiAt(int spine, uint32_t chars);
    bool placeOfCfi(const String& cfi, int& spine, uint32_t& chars);
    float percentAt(int spine, uint32_t chars) const;
    // About `maxBytes` of the chapter's text from character `chars` on (a bookmark's first words).
    String textAt(int spine, uint32_t chars, size_t maxBytes = 100);
    bool goToPosition(const String& cfi, const String& xpath, float percent, int page);
    void renderCurrentPage();
    int renderFailures() const { return _renderFailures; }   // pages that could not be drawn (tests, logs)

    int getFontSize() const { return _fontSize; }

    // How pages are laid out beyond the font (the Appearance menu). A change lays
    // the book out again at the same sentence.
    struct LayoutOptions {
        uint16_t lineSpacingPct = 100;       // line height, % of the font's
        uint16_t paragraphSpacingPct = 100;  // space between paragraphs, % of the book's
        uint8_t marginPx = 20;               // left and right
        bool justify = true;                 // else left-aligned
        bool hyphenate = true;
        bool keepLines = true;               // no lone first/last line of a paragraph at a page edge
        bool bookStyles = true;              // the book's CSS
        bool focusReading = false;           // the start of each word in bold
        bool operator==(const LayoutOptions& o) const {
            return lineSpacingPct == o.lineSpacingPct && paragraphSpacingPct == o.paragraphSpacingPct &&
                   marginPx == o.marginPx && justify == o.justify && hyphenate == o.hyphenate &&
                   keepLines == o.keepLines && bookStyles == o.bookStyles && focusReading == o.focusReading;
        }
    };
    bool setLayoutOptions(const LayoutOptions& options);   // false when nothing changed

    // The page on screen as a character range of its chapter [from, to) (to is
    // past the chapter for its last page), and its first words (bookmarks).
    bool pageRange(int& spine, uint32_t& from, uint32_t& to) const;
    const String& pageSnippet() const { return _snippet; }
    // Asked when the status bar is drawn: whether the page has a bookmark (a
    // ribbon is drawn in the corner).
    void setBookmarkProbe(bool (*probe)(int spine, uint32_t from, uint32_t to)) { _bookmarkProbe = probe; }
    const LayoutOptions& layoutOptions() const { return _opts; }
    // A full e-ink refresh every this many pages (0: never; ghosting stays until one).
    void setFullRefreshEvery(int pages) { _fullEvery = pages; }
    // Pictures in black and white: dithered (tones as dots, for photos) or sharp
    // (each pixel black or white at mid-gray, for line art and scans).
    void setSharpImages(bool sharp) { _sharpImages = sharp; }
    // Which way the pages are drawn: 0 portrait, 1 landscape (the panel's own),
    // 2 portrait upside down, 3 landscape upside down (each 90 degrees on from
    // the one before), as the whole screen is turned (app/orientation.h). A
    // landscape page is laid out wider; the book is laid out again at the same
    // sentence. The pages' own turn (the interface's and the book's together).
    // False when nothing changed.
    bool setOrientation(int orientation);
    // What the status bar shows (above and below the text). Hiding a line gives
    // its room to the text (the book is laid out again). False when nothing changed.
    struct StatusBar {
        uint8_t top = 0;       // top left: 0 book title, 1 chapter title, 2 nothing
        uint8_t pages = 0;     // 0 page in the book, 1 page in the chapter, 2 none
        bool percent = false;  // how much of the book is read
        bool battery = true;
        uint8_t bottom = 0;    // 0 chapter title and progress bar, 1 the bar, 2 nothing
        bool topShown() const { return top != 2 || pages != 2 || percent || battery; }
        bool operator==(const StatusBar& o) const {
            return top == o.top && pages == o.pages && percent == o.percent && battery == o.battery && bottom == o.bottom;
        }
    };
    bool setStatusBar(const StatusBar& bar);
    const StatusBar& statusBar() const { return _bar; }
    int orientation() const { return _orient; }
    bool landscape() const { return _orient == 1 || _orient == 3; }

    // The book font family: "" for the built-in one, else one installed from
    // /fonts (FontStore); another name falls back to the built-in one. Lays the
    // book out again at the same sentence. Returns false when nothing changed.
    bool setFontFamily(const String& name);
    String fontFamilyName() const;   // the family in use
    static const char* builtinFontName();
    bool usingBuiltinFont() const { return _family.length() == 0; }
    // Draws text in the book font (menus' previews): wrapped at spaces in the box,
    // the lines that fit. Returns the height used.
    int drawSample(int x, int y, int width, int height, const char* utf8, uint16_t sizePx,
                   uint16_t lineSpacingPct = 100);
    int sampleLineHeight(uint16_t sizePx) { return _fonts.lineHeight(sizePx); }
    // Lays out `text` (paragraphs split by blank lines) as a page would be with
    // these options and size, and draws what fits in the box: the Appearance
    // preview. Falls back to drawSample() when there is no memory to lay out.
    void drawPreview(int y, int height, const char* text, const LayoutOptions& options, int sizeIndex);
    bool setFontSize(int size);
    static const char* getFontSizeName(int size);

    // Most memory the reader's transient buffers (layout, parse, inflate, page
    // record) held at once since the last reset. The X4's biggest free block
    // after boot is ~96 KB: they must stay under it.
    static size_t peakTransientBytes(bool reset = false);

    // Copies an EPUB's cover image (as the package declares it) to `outPath`. Builds
    // the book's index if it has none (kept for opening the book). `noMemory` is set
    // when it failed for lack of memory (worth trying again later).
    // `cacheDir`: where the book's index lives (default: the EPUB's own; an EPUB
    // converted from another format sits in the cache directory of that file).
    static bool extractCover(const String& epubPath, const String& outPath, bool* noMemory = nullptr,
                             const String& cacheDir = String());

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
    // Gives back the font caches (~25 KB; Wi-Fi needs the memory): they fill again as pages are drawn.
    void releaseFontCaches() { _ttf.release(); }
    static constexpr int kLookahead = 2;   // chapters ahead of the current one
    // Pages of a laid-out spine item, -1 if it is not laid out (tests, logs).
    int chapterPages(int spine) const {
        return spine >= 0 && spine < (int)_spines.size() ? (int)_spines[spine].pages : -1;
    }
    int spineCount() const { return (int)_spines.size(); }

private:
    static void drawConvertProgress(void* self, int permille);
    int _convertStep = -1;
    uint32_t _convertDrawnAt = 0;

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
    void buildUntilPages(uint32_t count);
    void sayCatchingUp(uint32_t pagesLeft);     // waits until `count` pages exist (or the chapter ends)
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
    // CFIs in and out of the reader are the web reader's: for an FB2 book they go
    // through the converter's map (identity for others).
    String toPublicCfi(const String& local) const;
    String toLocalCfi(const String& pub) const;
    bool cfiToChapterChar(const String& cfi, int& spine, uint32_t& chars);
    void drawStatusBar();
    void releaseIndex();

    bool _opened = false;
    CfiMap _cfiMap;                 // FB2: the converted EPUB's places in the web reader's layout
    bool _noMemory = false;
    uint8_t _convertProblem = 0;   // convert::ConvertProblem
    String _path;
    String _title;
    int _fontSize;

    SdBookSource _src;
    SdBookSource _chapterSrc;   // an unpacked chapter while it is laid out or walked
    SdCacheStorage _cache;
    freeink::book::BookCatalog _catalog;
    uint8_t* _bookMem = nullptr;
    freeink::book::Arena _bookArena;

    BitmapBookFont _font;            // what the TrueType faces lack (and the font with none)
    TtfFamily _ttf;                  // the family in use (built-in, or installed from the card)
    String _family;                  // its name ("" = built-in)
    freeink::book::FontChain _fonts;
    void setupFonts(const String& family);
    freeink::book::Hyphenator _hyphEn;
    freeink::book::Hyphenator _hyphRu;
    // Picks the patterns by the book's language ("ru", "ru-RU", "rus", "en"...); a
    // plain-text file has none, so its first kilobytes are looked at instead.
    const char* bookLanguage();
    const freeink::book::Hyphenator* hyphenatorFor(const char* language) const;
    freeink::book::LayoutParams _params;
    LayoutOptions _opts;
    String _snippet;
    bool (*_bookmarkProbe)(int, uint32_t, uint32_t) = nullptr;
    int _fullEvery = FULL_REFRESH_EVERY_PAGES;
    bool _sharpImages = false;
    int _orient = 0;
    StatusBar _bar;
public:
    freeink::book::FrameRotation frameRotation() const;   // the screen's turn, for the book's frames
private:
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
    // Where the page on screen starts, kept when its layout job is stopped (for a
    // sync, sleep): its position is then known without laying the chapter out again.
    struct Held { int spine = -1; uint32_t page = 0, chars = 0; } _held;
    bool heldHere() const { return _want == Want::Page && _held.spine == _spine && _held.page == _wantValue; }
    float _wantFraction = 0.0f;   // Want::Fraction: share of the chapter's characters

    // getCfi() is a chapter walk: cached for the page it was made for.
    int _cfiSpine = -1;
    uint32_t _cfiChars = UINT32_MAX;
    String _cfi;
    int _renderedPages = 0;
    int _renderFailures = 0;
};
