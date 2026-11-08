#pragma once

#include <Arduino.h>
#include <vector>
#include "../hal/storage.h"
#include "epub_extractor.h"

struct ChapterItem {
    String title;
    uint32_t byteOffset;
    int page;
};

class SimpleReader {
public:
    SimpleReader();

    bool openBook(const String& filePath);
    void closeBook();

    bool isOpened() const { return _isOpened; }
    String getTitle() const { return _bookTitle; }
    String getOriginalFilePath() const { return _originalFilePath; }
    String getTextFilePath() const { return _textFilePath; }
    float getProgress() const;
    int getCurrentPage() const { return _currentPageIndex + 1; }
    int getTotalPages() const { return max(1, (int)_pageOffsets.size()); }

    // Chapter Navigation
    bool hasChapters() const { return !_chapters.empty(); }
    int getChapterCount() const { return (int)_chapters.size(); }
    const std::vector<ChapterItem>& getChapters() const { return _chapters; }
    int getCurrentChapterIndex() const;
    String getCurrentChapterTitle() const;
    bool nextChapter();
    bool prevChapter();
    bool goToChapter(int chapterIndex);

    // Page Navigation
    bool nextPage();
    bool prevPage();
    bool setPage(int page);
    bool setPageByProgress(float percent);
    // `fallbackPercent` is used when the xpath is unusable or disagrees with it
    // by more than 1.5% (stale xpath from another reader).
    bool setPageByXPath(const String& xpath, float fallbackPercent = 0.0f);
    String getXPath() const;   // exact page start: spine item, paragraph, char offset

    // EPUB CFI of the Book Orbit web reader. `fallbackPercent` rejects a stale CFI
    // (more than 3% away, e.g. written before another reader moved on); returns
    // false then, or when the CFI is unusable, without moving.
    bool setPageByCfi(const String& cfi, float fallbackPercent);
    String getCfi();           // page start as a CFI; "" for plain text or on failure

    // Restores a saved position: CFI > xpath (both checked against percent) > percent > page.
    bool goToPosition(const String& cfi, const String& xpath, float percent, int page);
    void renderCurrentPage();

    // Font Configuration
    int getFontSize() const { return _fontSize; }
    bool setFontSize(int size);
    static const char* getFontSizeName(int size);

private:
    bool _statusShown = false;  // a status screen is already up during openBook
    bool _isOpened;
    int _fontSize;
    String _originalFilePath;
    String _textFilePath;
    String _bookTitle;
    size_t _fileSize;

    std::vector<uint32_t> _pageOffsets;
    int _currentPageIndex;

    std::vector<ChapterItem> _chapters;

    struct SpineItemInfo {
        int spineIndex;
        uint32_t byteOffset;
        String title;
    };
    std::vector<SpineItemInfo> _spineItems;

    // CFI support (EPUB only): chapter -> <itemref> number / file, loaded on first use.
    std::vector<EpubExtractor::CfiChapter> _cfiMap;
    bool _cfiMapLoaded = false;
    uint32_t _cfiCacheOffset = UINT32_MAX;   // page start the cached CFI belongs to
    String _cfiCache;
    const EpubExtractor::CfiChapter* cfiChapter(int spineIndex, int ordinal);

    void paginateFile();
    void loadChapters();
    void loadSpine();
    void scanTextForChapters();
    int offsetToPage(uint32_t byteOffset) const;
    // Spine items (from the EPUB) or, for plain text, TOC chapters.
    bool spineItemStart(int spineIndex, uint32_t& outOffset) const;
    int spineItemAt(uint32_t offset, uint32_t& outStart) const;
    void drawStatusBar();
};

extern SimpleReader Reader;
