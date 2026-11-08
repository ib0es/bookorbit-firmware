#pragma once

#include <Arduino.h>
#include <vector>
#include "../hal/storage.h"
#include "epub_cfi.h"

struct ExtractedChapter {
    String title;
    uint32_t byteOffset;
};

class EpubExtractor {
public:
    static bool isEpub(const String& filePath);
    static String getCachePath(const String& filePath);
    static String getCoverPath(const String& filePath);
    static String getTocPath(const String& filePath);
    static String getSpinePath(const String& filePath);

    // Extracts only the embedded cover image from an EPUB to outCoverPath (e.g. /covers/<basename>.jpg).
    // Fast (~20ms), does not unpack or parse chapters.
    static bool extractCoverOnly(const String& epubPath, const String& outCoverPath);

    // Extracts EPUB XHTML chapters into a clean, formatted plain-text cache file on MicroSD.
    // Also extracts embedded cover image to /covers/ and TOC to /cache/<basename>.toc if found.
    // Returns true on success.
    static bool extractToText(const String& epubPath, const String& outTxtPath, String* outCoverPath = nullptr);

    // True if the last extractToText()/extractCoverOnly() failed for lack of memory.
    static bool lastFailedForMemory();

    // --- EPUB CFI (positions of the Book Orbit web reader) ---------------------
    // A converted chapter: index in /cache/<book>.spine, its <itemref> number in
    // the OPF spine (the "/6/<2n>" of a CFI; 0 = unknown) and its file in the EPUB.
    struct CfiChapter {
        int spineIndex;
        int ordinal;
        String zipPath;
    };
    static String getCfiMapPath(const String& filePath);
    // Reads /cache/<book>.cfimap; for caches made before it existed, rebuilds it
    // from the EPUB's OPF (and saves it).
    static bool loadCfiMap(const String& epubPath, std::vector<CfiChapter>& out);
    // CFI path inside a chapter -> byte offset in that chapter's converted text.
    // false if the chapter cannot be read; `reached` false if the point is past its end.
    static bool cfiToChapterOffset(const String& epubPath, const String& zipPath, const EpubCfi::Location& loc,
                                   uint32_t& outOffset, bool* reached = nullptr);
    // Byte offset in a chapter's converted text -> CFI steps/offset (ordinal not set).
    static bool chapterOffsetToCfi(const String& epubPath, const String& zipPath, uint32_t offset,
                                   EpubCfi::Location& out);
};
