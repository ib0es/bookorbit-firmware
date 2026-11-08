#include "simple_reader.h"
#include "../ui/kit/ui_kit.h"
#include "../ui/views/status_modal.h"
#include "../services/progress_sync.h"
#include "epub_extractor.h"
#include "koreader_sync_mapper.h"
#include "../hal/display.h"
#include "../hal/battery.h"
#include "../ui/font.h"
#include "../ui/i18n.h"
#include "config.h"

// Page index cache: one per text file, font size (px) and screen text box, so
// a card moved between devices never reuses a pagination made for another
// screen or font. (Older firmware wrote <text>_f<0..3>[_WxH].idx for its four
// U8g2 fonts; those are removed by LibraryManager::cleanOrphanFiles.)
static String pageIndexPath(const String& textPath, int fontSize) {
    const BookPageGeometry g = bookPageGeometry();
    return textPath + "_s" + String(FontEngine::sizePx(fontSize)) + "_" + String(g.width) + "x" + String(g.maxY) + ".idx";
}

SimpleReader Reader;

SimpleReader::SimpleReader() 
    : _isOpened(false), _fontSize(FontEngine::kDefaultSize), _fileSize(0), _currentPageIndex(0) {}

const char* SimpleReader::getFontSizeName(int size) {
    static char buf[8][12];   // a few names may be alive at once (menus)
    static int next = 0;
    char* out = buf[next++ & 7];
    snprintf(out, sizeof(buf[0]), "%d pt", FontEngine::sizePx(size));
    return out;
}

bool SimpleReader::setFontSize(int size) {
    size = FontEngine::clampSize(size);
    if (size == _fontSize) return false;

    uint32_t currentByteOffset = 0;
    if (!_pageOffsets.empty() && _currentPageIndex >= 0 && _currentPageIndex < (int)_pageOffsets.size()) {
        currentByteOffset = _pageOffsets[_currentPageIndex];
    }

    _fontSize = size;

    if (_isOpened) {
        _statusShown = false;
        paginateFile();
        for (auto& ch : _chapters) {
            ch.page = offsetToPage(ch.byteOffset);
        }
        int newPage = offsetToPage(currentByteOffset);
        setPage(newPage);
    }
    return true;
}

bool SimpleReader::openBook(const String& filePath) {
    if (!Storage.exists(filePath.c_str())) {
        Serial.printf("[Reader] File not found: %s\n", filePath.c_str());
        return false;
    }

    _originalFilePath = filePath;
    _statusShown = false;
    _cfiMap.clear();
    _cfiMapLoaded = false;
    _cfiCacheOffset = UINT32_MAX;
    _cfiCache = "";
    
    // Extract title from filename
    int lastSlash = filePath.lastIndexOf('/');
    _bookTitle = (lastSlash >= 0) ? filePath.substring(lastSlash + 1) : filePath;
    int dotIdx = _bookTitle.lastIndexOf('.');
    if (dotIdx > 0) {
        _bookTitle = _bookTitle.substring(0, dotIdx);
    }

    // Handle EPUB format
    if (EpubExtractor::isEpub(filePath)) {
        String cachePath = EpubExtractor::getCachePath(filePath);
        String tocPath = EpubExtractor::getTocPath(filePath);
        
        bool needExtract = true;
        // Caches from before the .spine index cannot map synced positions (CFI /
        // xpath) to chapters: such books are converted once more.
        const bool hasSpine = Storage.exists(EpubExtractor::getSpinePath(filePath).c_str());
        if (!hasSpine) Serial.println("[Reader] Cache has no spine index: converting the book again.");
        if (hasSpine && Storage.exists(cachePath.c_str()) && Storage.exists(tocPath.c_str())) {
            FsFile cf = Storage.open(cachePath.c_str(), O_RDONLY);
            FsFile tf = Storage.open(tocPath.c_str(), O_RDONLY);
            if (cf && tf) {
                if (cf.size() > 100) {
                    char headerBuf[32];
                    size_t hn = tf.readBytesUntil('\n', headerBuf, sizeof(headerBuf) - 1);
                    headerBuf[hn] = '\0';
                    if (hn > 0 && headerBuf[hn - 1] == '\r') headerBuf[hn - 1] = '\0';
                    if (strncmp(headerBuf, "# TOC_V2", 8) == 0) {
                        needExtract = false;
                    }
                }
            }
            if (cf) cf.close();
            if (tf) tf.close();
        }

        if (needExtract) {
            Serial.printf("[Reader] Extracting EPUB to cache: %s\n", cachePath.c_str());
            
            // Clean up any stale partial files and old font pagination indices
            Storage.remove(cachePath.c_str());
            Storage.remove(tocPath.c_str());
            Storage.remove((cachePath + ".idx").c_str());
            Storage.remove((cachePath + "_f0.idx").c_str());
            Storage.remove((cachePath + "_f1.idx").c_str());
            Storage.remove((cachePath + "_f2.idx").c_str());
            Storage.remove((cachePath + "_f3.idx").c_str());
            for (int fs = 0; fs < FontEngine::kSizeCount; fs++) Storage.remove(pageIndexPath(cachePath, fs).c_str());
            Storage.remove((cachePath + ".tmp").c_str());
            Storage.remove((tocPath + ".tmp").c_str());

            // Show status modal on screen (full refresh: unpacking takes a while)
            _statusShown = true;
            drawStatusModal(tr(MsgId::READER_OPENING_TITLE), _bookTitle.c_str(), tr(MsgId::READER_OPENING_UNPACK),
                            tr(MsgId::READER_OPENING_WAIT), nullptr, nullptr, true);

            if (!EpubExtractor::extractToText(filePath, cachePath)) {
                Serial.println("[Reader] EPUB extraction failed!");
                Storage.remove(cachePath.c_str());
                Storage.remove(tocPath.c_str());
                return false;
            }
        } else {
            // Cache already exists; ensure cover is extracted if missing
            String coverDst = EpubExtractor::getCoverPath(filePath);
            if (!Storage.exists(coverDst.c_str())) {
                EpubExtractor::extractCoverOnly(filePath, coverDst);
            }
        }
        _textFilePath = cachePath;
    } else {
        // Plain text file
        _textFilePath = filePath;
    }

    FsFile f = Storage.open(_textFilePath.c_str(), O_RDONLY);
    if (!f) {
        Serial.printf("[Reader] Cannot open text file: %s\n", _textFilePath.c_str());
        return false;
    }

    _fileSize = f.size();
    f.close();

    if (_fileSize == 0) {
        Serial.printf("[Reader] File is empty: %s\n", _textFilePath.c_str());
        return false;
    }

    _pageOffsets.clear();
    _currentPageIndex = 0;

    // Load chapters before pagination so chapter offsets trigger page breaks
    loadChapters();
    loadSpine();

    paginateFile();

    // Update chapter page numbers with final pagination
    for (auto& ch : _chapters) {
        ch.page = offsetToPage(ch.byteOffset);
    }

    if (_pageOffsets.empty() || _fileSize == 0) {
        Serial.printf("[Reader] Book opening failed: offsets=%d size=%d\n", (int)_pageOffsets.size(), (int)_fileSize);
        _isOpened = false;
        return false;
    }

    _isOpened = true;
    return true;
}

void SimpleReader::closeBook() {
    _isOpened = false;
    _pageOffsets.clear();
    _chapters.clear();
    _spineItems.clear();
    _cfiMap.clear();
    _cfiMapLoaded = false;
    _cfiCacheOffset = UINT32_MAX;
    _cfiCache = "";
    _currentPageIndex = 0;
}

int SimpleReader::offsetToPage(uint32_t byteOffset) const {
    if (_pageOffsets.empty()) return 1;
    int page = 1;
    for (size_t i = 0; i < _pageOffsets.size(); i++) {
        if (_pageOffsets[i] <= byteOffset) {
            page = (int)i + 1;
        } else {
            break;
        }
    }
    return page;
}

void SimpleReader::loadChapters() {
    _chapters.clear();
    String tocPath = EpubExtractor::getTocPath(_originalFilePath);

    if (Storage.exists(tocPath.c_str())) {
        FsFile tocFile = Storage.open(tocPath.c_str(), O_RDONLY);
        if (tocFile) {
            char lineBuf[256];
            while (tocFile.available()) {
                size_t n = tocFile.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
                lineBuf[n] = '\0';
                if (n > 0 && lineBuf[n - 1] == '\r') {
                    lineBuf[n - 1] = '\0';
                    n--;
                }
                if (lineBuf[0] == '#' || n == 0) continue;

                char* tab = strchr(lineBuf, '\t');
                if (tab) {
                    *tab = '\0';
                    uint32_t ofs = strtoul(lineBuf, NULL, 10);
                    const char* title = tab + 1;
                    while (*title == ' ' || *title == '\t') title++;

                    ChapterItem item;
                    item.title = String(title);
                    item.byteOffset = ofs;
                    item.page = offsetToPage(ofs);
                    _chapters.push_back(item);
                }
            }
            tocFile.close();
        }
    }

    if (_chapters.empty()) {
        scanTextForChapters();
    }

    Serial.printf("[Reader] Loaded %d chapters for navigation.\n", _chapters.size());
}

void SimpleReader::loadSpine() {
    _spineItems.clear();
    String spinePath = EpubExtractor::getSpinePath(_originalFilePath);

    if (Storage.exists(spinePath.c_str())) {
        FsFile spineFile = Storage.open(spinePath.c_str(), O_RDONLY);
        if (spineFile) {
            char lineBuf[256];
            while (spineFile.available()) {
                size_t n = spineFile.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
                lineBuf[n] = '\0';
                if (n > 0 && lineBuf[n - 1] == '\r') {
                    lineBuf[n - 1] = '\0';
                    n--;
                }
                if (lineBuf[0] == '#' || n == 0) continue;

                char* tab1 = strchr(lineBuf, '\t');
                if (tab1) {
                    *tab1 = '\0';
                    int sIdx = atoi(lineBuf);
                    char* tab2 = strchr(tab1 + 1, '\t');
                    uint32_t ofs = 0;
                    String sTitle = "";
                    if (tab2) {
                        *tab2 = '\0';
                        ofs = strtoul(tab1 + 1, NULL, 10);
                        sTitle = String(tab2 + 1);
                    } else {
                        ofs = strtoul(tab1 + 1, NULL, 10);
                    }
                    _spineItems.push_back({ sIdx, ofs, sTitle });
                }
            }
            spineFile.close();
        }
    }

    // Fallback: if no .spine file exists, synthesize from chapters
    if (_spineItems.empty()) {
        for (size_t i = 0; i < _chapters.size(); i++) {
            _spineItems.push_back({ (int)i, _chapters[i].byteOffset, _chapters[i].title });
        }
    }

    if (_spineItems.empty()) {
        _spineItems.push_back({ 0, 0, tr(MsgId::READER_TOC_START) });
    }

    Serial.printf("[Reader] Loaded %d spine chapters for KOReader sync.\n", (int)_spineItems.size());
}

void SimpleReader::scanTextForChapters() {
    FsFile f = Storage.open(_textFilePath.c_str(), O_RDONLY);
    if (!f) return;

    _chapters.clear();
    char lineBuf[256];
    uint32_t lineOffset = 0;
    int lineCount = 0;

    while (f.available()) {
        if ((++lineCount % 50) == 0) {
            yield();
            vTaskDelay(1);
        }
        lineOffset = f.position();
        size_t n = f.readBytesUntil('\n', lineBuf, sizeof(lineBuf) - 1);
        lineBuf[n] = '\0';
        if (n > 0 && lineBuf[n - 1] == '\r') {
            lineBuf[n - 1] = '\0';
            n--;
        }

        char* p = lineBuf;
        while (*p == ' ' || *p == '\t') p++;
        if (strlen(p) == 0) continue;

        bool isChapter = false;
        if (strncmp(p, "Глава ", 11) == 0 || strncmp(p, "ГЛАВА ", 11) == 0 ||
            strncmp(p, "Часть ", 11) == 0 || strncmp(p, "ЧАСТЬ ", 11) == 0 ||
            strncmp(p, "Пролог", 12) == 0 || strncmp(p, "ПРОЛОГ", 12) == 0 ||
            strncmp(p, "Эпилог", 12) == 0 || strncmp(p, "ЭПИЛОГ", 12) == 0 ||
            strncmp(p, "Раздел ", 13) == 0 || strncmp(p, "РАЗДЕЛ ", 13) == 0 ||
            strncmp(p, "Chapter ", 8) == 0 || strncmp(p, "CHAPTER ", 8) == 0 ||
            strncmp(p, "Book ", 5) == 0 || strncmp(p, "BOOK ", 5) == 0) {
            isChapter = true;
        }

        if (isChapter && strlen(p) < 90) {
            ChapterItem item;
            item.title = String(p);
            item.byteOffset = lineOffset;
            item.page = offsetToPage(lineOffset);
            _chapters.push_back(item);
        }
    }
    f.close();

    // If still empty and book has multiple pages, create milestone checkpoints
    if (_chapters.empty() && getTotalPages() > 8) {
        for (int p = 1; p <= getTotalPages(); p += 15) {
            ChapterItem item;
            char buf[48];
            snprintf(buf, sizeof(buf), tr(MsgId::READER_TOC_SECTION_PAGE_FMT), p);
            item.title = String(buf);
            item.page = p;
            item.byteOffset = (p - 1 < (int)_pageOffsets.size()) ? _pageOffsets[p - 1] : 0;
            _chapters.push_back(item);
        }
    }

    if (_chapters.empty()) {
        ChapterItem item;
        item.title = tr(MsgId::READER_TOC_START);
        item.page = 1;
        item.byteOffset = 0;
        _chapters.push_back(item);
    }

    // Persist discovered chapters to TOC cache
    String tocPath = EpubExtractor::getTocPath(_originalFilePath);
    FsFile tocFile = Storage.open(tocPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (tocFile) {
        for (const auto& ch : _chapters) {
            tocFile.printf("%u\t%s\n", (unsigned int)ch.byteOffset, ch.title.c_str());
        }
        tocFile.close();
    }
}

int SimpleReader::getCurrentChapterIndex() const {
    if (_chapters.empty() || _pageOffsets.empty()) return 0;
    int curPage = getCurrentPage();
    int curIdx = 0;
    for (size_t i = 0; i < _chapters.size(); i++) {
        if (_chapters[i].page <= curPage) {
            curIdx = (int)i;
        } else {
            break;
        }
    }
    return curIdx;
}

String SimpleReader::getCurrentChapterTitle() const {
    if (_chapters.empty()) return "";
    int idx = getCurrentChapterIndex();
    if (idx >= 0 && idx < (int)_chapters.size()) {
        return _chapters[idx].title;
    }
    return "";
}

bool SimpleReader::nextChapter() {
    if (!_isOpened || _chapters.empty()) return false;
    int curIdx = getCurrentChapterIndex();
    if (curIdx + 1 < (int)_chapters.size()) {
        setPage(_chapters[curIdx + 1].page);
        renderCurrentPage();
        return true;
    }
    return false;
}

bool SimpleReader::prevChapter() {
    if (!_isOpened || _chapters.empty()) return false;
    int curIdx = getCurrentChapterIndex();
    int curPage = getCurrentPage();

    // If more than 1 page into current chapter, jump back to start of current chapter
    if (curPage > _chapters[curIdx].page) {
        setPage(_chapters[curIdx].page);
        renderCurrentPage();
        return true;
    } else if (curIdx > 0) {
        setPage(_chapters[curIdx - 1].page);
        renderCurrentPage();
        return true;
    }
    return false;
}

bool SimpleReader::goToChapter(int chapterIndex) {
    if (!_isOpened || chapterIndex < 0 || chapterIndex >= (int)_chapters.size()) return false;
    setPage(_chapters[chapterIndex].page);
    return true;
}

void SimpleReader::paginateFile() {
    uint32_t t0 = millis();

    const uint32_t IDX_MAGIC = 0x50414734; // "PAG4" header magic
    String idxPath = pageIndexPath(_textFilePath, _fontSize);

    // 1. Fast binary index loading: opens instantly in < 10ms!
    if (Storage.exists(idxPath.c_str())) {
        FsFile idxFile = Storage.open(idxPath.c_str(), O_RDONLY);
        if (idxFile) {
            uint32_t magic = 0;
            uint32_t savedSize = 0;
            if (idxFile.read(&magic, sizeof(magic)) == (int)sizeof(magic) && magic == IDX_MAGIC &&
                idxFile.read(&savedSize, sizeof(savedSize)) == (int)sizeof(savedSize) && savedSize == (uint32_t)_fileSize) {
                uint32_t count = 0;
                if (idxFile.read(&count, sizeof(count)) == (int)sizeof(count) && count > 0 && count < 100000) {
                    _pageOffsets.resize(count);
                    size_t bytesToRead = count * sizeof(uint32_t);
                    if (idxFile.read(_pageOffsets.data(), bytesToRead) == (int)bytesToRead) {
                        idxFile.close();
                        Serial.printf("[Reader] Fast index loaded: %d pages from %s in %lu ms.\n", 
                                      (int)count, idxPath.c_str(), millis() - t0);
                        return;
                    }
                }
            }
            idxFile.close();
        }
    }

    // 2. Compute accurate line-based pagination to fill the entire 480x800 display
    if (!_statusShown) {
        // No index for this font/screen yet (new font size, or a card that came
        // from another device): say so instead of freezing.
        _statusShown = true;
        drawStatusModal(tr(MsgId::READER_FORMAT_TITLE), _bookTitle.substring(0, 32).c_str(),
                        tr(MsgId::READER_FORMAT_LINE), tr(MsgId::READER_OPENING_WAIT));
    }
    FsFile f = Storage.open(_textFilePath.c_str(), O_RDONLY);
    if (!f) {
        if (_pageOffsets.empty()) _pageOffsets.push_back(0);
        return;
    }

    _pageOffsets.clear();
    _pageOffsets.push_back(0); // Page 1 starts at byte 0

    // Collect chapter byte offsets for chapter-break pagination
    std::vector<uint32_t> chOffsets;
    for (const auto& ch : _chapters) {
        if (ch.byteOffset > 0 && ch.byteOffset < (uint32_t)_fileSize) {
            chOffsets.push_back((uint32_t)ch.byteOffset);
        }
    }
    std::sort(chOffsets.begin(), chOffsets.end());
    size_t nextChIdx = 0;

    int bodyScale = FontEngine::clampSize(_fontSize);
    int headerScale = FontEngine::headingSize(bodyScale);

    Font.selectFont(bodyScale);
    int lineHeight = Font.getFontHeight(bodyScale);
    int spaceWidth = Font.getTextWidth(" ", bodyScale);
    int headerLineHeight = Font.getFontHeight(headerScale);
    int headerHeight = headerLineHeight + 8;

    const BookPageGeometry geo = bookPageGeometry();
    const int START_Y = geo.top;
    const int MAX_Y = geo.maxY;
    const int MAX_WIDTH = geo.width;
    const int PARAGRAPH_INDENT = FontEngine::paragraphIndent(bodyScale);

    const size_t BUF_SIZE = 4096;
    char* buf = (char*)malloc(BUF_SIZE);
    if (!buf) {
        f.close();
        if (_pageOffsets.empty()) _pageOffsets.push_back(0);
        return;
    }

    uint32_t windowFileOffset = 0;
    size_t inBuf = 0;
    size_t bufIdx = 0;

    auto refill = [&]() {
        if (bufIdx > 0 && inBuf > bufIdx) {
            size_t remaining = inBuf - bufIdx;
            memmove(buf, buf + bufIdx, remaining);
            windowFileOffset += bufIdx;
            inBuf = remaining;
            bufIdx = 0;
        } else if (bufIdx >= inBuf) {
            windowFileOffset += bufIdx;
            inBuf = 0;
            bufIdx = 0;
        }
        if (inBuf < BUF_SIZE && (windowFileOffset + inBuf < _fileSize)) {
            f.seek(windowFileOffset + inBuf);
            size_t toRead = BUF_SIZE - inBuf;
            size_t n = f.read(buf + inBuf, toRead);
            inBuf += n;
        }
    };

    refill();

    while (windowFileOffset + bufIdx < _fileSize) {
        if ((_pageOffsets.size() % 5) == 0) {
            yield();
            vTaskDelay(1);
        }

        uint32_t pageStart = windowFileOffset + bufIdx;
        int curY = START_Y;
        int curX = 0;
        bool isNewParagraph = true;
        bool isBold = false;

        while ((windowFileOffset + bufIdx < _fileSize) && curY <= MAX_Y) {
            if (inBuf - bufIdx < 300) refill();
            if (bufIdx >= inBuf) break;

            uint32_t curFileOfs = windowFileOffset + bufIdx;

            // Check if reached a new chapter boundary -> force page break!
            while (nextChIdx < chOffsets.size() && chOffsets[nextChIdx] < curFileOfs) nextChIdx++;
            if (nextChIdx < chOffsets.size() && chOffsets[nextChIdx] == curFileOfs && curFileOfs > pageStart) {
                break;
            }

            char c = buf[bufIdx];
            if (c == '\r') {
                bufIdx++;
                continue;
            }

            if (c == '\f') {
                bufIdx++;
                break; // Form feed: force page break!
            }

            // Start of line
            if (curX == 0) {
                while (windowFileOffset + bufIdx < _fileSize && (buf[bufIdx] == ' ' || buf[bufIdx] == '\t')) {
                    bufIdx++;
                    if (inBuf - bufIdx < 50) refill();
                }
                if (bufIdx >= inBuf) break;

                // Check for heading
                bool isHeading = false;
                if (buf[bufIdx] == '#') {
                    isHeading = true;
                } else if (inBuf - bufIdx > 12) {
                    const char* p = buf + bufIdx;
                    if (strncmp(p, "Глава ", 11) == 0 || strncmp(p, "ГЛАВА ", 11) == 0 ||
                        strncmp(p, "Часть ", 11) == 0 || strncmp(p, "ЧАСТЬ ", 11) == 0 ||
                        strncmp(p, "Пролог", 12) == 0 || strncmp(p, "ПРОЛОГ", 12) == 0 ||
                        strncmp(p, "Эпилог", 12) == 0 || strncmp(p, "ЭПИЛОГ", 12) == 0 ||
                        strncmp(p, "Chapter ", 8) == 0 || strncmp(p, "CHAPTER ", 8) == 0) {
                        isHeading = true;
                    }
                }

                if (isHeading) {
                    while (windowFileOffset + bufIdx < _fileSize && buf[bufIdx] != '\n' && buf[bufIdx] != '\r' && buf[bufIdx] != '\f') {
                        bufIdx++;
                        if (inBuf - bufIdx < 50) refill();
                    }
                    curY += headerHeight;
                    curX = 0;
                    isNewParagraph = true;
                    while (windowFileOffset + bufIdx < _fileSize && (buf[bufIdx] == '\n' || buf[bufIdx] == '\r')) {
                        bufIdx++;
                        if (inBuf - bufIdx < 50) refill();
                    }
                    continue;
                }

                if (isNewParagraph) {
                    curX = PARAGRAPH_INDENT;
                    isNewParagraph = false;
                }
            }

            // Check for bold toggle "**"
            if (buf[bufIdx] == '*' && bufIdx + 1 < inBuf && buf[bufIdx + 1] == '*') {
                isBold = !isBold;
                bufIdx += 2;
                continue;
            }

            if (buf[bufIdx] == '\n') {
                bufIdx++;
                curX = 0;
                curY += lineHeight;
                if (inBuf - bufIdx < 50) refill();
                if (bufIdx < inBuf && (buf[bufIdx] == '\n' || buf[bufIdx] == '\r')) {
                    while (bufIdx < inBuf && (buf[bufIdx] == '\n' || buf[bufIdx] == '\r')) {
                        bufIdx++;
                        if (inBuf - bufIdx < 50) refill();
                    }
                    curY += 8;
                }
                isNewParagraph = true;
                continue;
            }

            if (buf[bufIdx] == ' ') {
                bufIdx++;
                curX += spaceWidth;
                continue;
            }

            // Read word
            size_t wStartIdx = bufIdx;
            while ((windowFileOffset + bufIdx < _fileSize) && 
                   buf[bufIdx] != ' ' && buf[bufIdx] != '\n' && buf[bufIdx] != '\r' && buf[bufIdx] != '\f') {
                if (buf[bufIdx] == '*' && bufIdx + 1 < inBuf && buf[bufIdx + 1] == '*') break;
                bufIdx++;
                if (bufIdx >= inBuf) {
                    size_t wPart = bufIdx - wStartIdx;
                    refill();
                    wStartIdx = 0;
                    bufIdx = wPart;
                    if (bufIdx >= inBuf) break;
                }
            }
            size_t wLen = bufIdx - wStartIdx;

            char wordBuf[256];
            size_t copyLen = (wLen < sizeof(wordBuf) - 1) ? wLen : sizeof(wordBuf) - 1;
            memcpy(wordBuf, buf + wStartIdx, copyLen);
            wordBuf[copyLen] = '\0';

            int wPx = Font.getTextWidth(wordBuf, bodyScale);
            int extraBold = isBold ? 1 : 0;

            if (curX + wPx + extraBold > MAX_WIDTH && curX > 0) {
                curX = 0;
                curY += lineHeight;
            }

            if (curY > MAX_Y) {
                bufIdx = wStartIdx; // Roll back word to start of next page
                break;
            }

            curX += wPx + extraBold;
        }

        // Skip consecutive newlines / formfeeds at page boundary
        while (windowFileOffset + bufIdx < _fileSize) {
            if (inBuf - bufIdx < 10) refill();
            if (bufIdx >= inBuf) break;
            if (buf[bufIdx] == '\n' || buf[bufIdx] == '\r' || buf[bufIdx] == '\f') {
                bufIdx++;
            } else {
                break;
            }
        }

        uint32_t nextPageOffset = windowFileOffset + bufIdx;
        if (nextPageOffset > pageStart && nextPageOffset < _fileSize) {
            _pageOffsets.push_back(nextPageOffset);
        } else if (nextPageOffset == pageStart) {
            bufIdx++;
        }
    }

    free(buf);
    f.close();

    if (_pageOffsets.empty()) {
        _pageOffsets.push_back(0);
    }

    Serial.printf("[Reader] Paginating complete. Total pages: %d (took %lu ms)\n", 
                  (int)_pageOffsets.size(), millis() - t0);

    // 3. Save computed pagination index with PAG3 magic for instant open next time
    if (!_pageOffsets.empty()) {
        FsFile idxOut = Storage.open(idxPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if (idxOut) {
            uint32_t magic = IDX_MAGIC;
            uint32_t fsize = (uint32_t)_fileSize;
            uint32_t count = (uint32_t)_pageOffsets.size();
            idxOut.write((const uint8_t*)&magic, sizeof(magic));
            idxOut.write((const uint8_t*)&fsize, sizeof(fsize));
            idxOut.write((const uint8_t*)&count, sizeof(count));
            idxOut.write((const uint8_t*)_pageOffsets.data(), count * sizeof(uint32_t));
            idxOut.close();
            Serial.printf("[Reader] Saved pagination cache to %s (%d bytes)\n", idxPath.c_str(), (int)(count * sizeof(uint32_t)));
        }
    }
}

bool SimpleReader::nextPage() {
    if (!_isOpened || _currentPageIndex + 1 >= (int)_pageOffsets.size()) return false;
    _currentPageIndex++;
    renderCurrentPage();
    return true;
}

bool SimpleReader::prevPage() {
    if (!_isOpened || _currentPageIndex <= 0) return false;
    _currentPageIndex--;
    renderCurrentPage();
    return true;
}

bool SimpleReader::setPage(int page) {
    if (!_isOpened || _pageOffsets.empty()) return false;
    if (page < 1) page = 1;
    if (page > (int)_pageOffsets.size()) page = (int)_pageOffsets.size();
    _currentPageIndex = page - 1;
    return true;
}

// ---------------------------------------------------------------------------
// Position <-> percent / KOReader xpath.
//
// Invariants (checked by the headless round-trip test):
//   setPageByProgress(getProgress())            -> same page
//   setPageByXPath(getXPath(), getProgress())   -> same page
// The xpath encodes spine item, paragraph (blank-line separated block in the
// extracted text) and the UTF-8 character offset inside that paragraph, so it
// points at the exact first character of the page, not at its paragraph start.
// ---------------------------------------------------------------------------

namespace {
// An xpath whose target is further than this from the reported percentage is
// considered stale (e.g. the web reader changed the percentage but left the old
// KOReader xpath) and the percentage is used instead.
constexpr float XPATH_TRUST_TOLERANCE = 0.015f;

inline bool isUtf8Lead(uint8_t b) { return (b & 0xC0) != 0x80; }
}  // namespace

bool SimpleReader::spineItemStart(int spineIndex, uint32_t& outOffset) const {
    if (!_spineItems.empty()) {
        for (const auto& s : _spineItems) {
            if (s.spineIndex == spineIndex) { outOffset = s.byteOffset; return true; }
        }
        return false;
    }
    if (spineIndex >= 0 && spineIndex < (int)_chapters.size()) {
        outOffset = _chapters[spineIndex].byteOffset;
        return true;
    }
    return false;
}

int SimpleReader::spineItemAt(uint32_t offset, uint32_t& outStart) const {
    int idx = 0;
    outStart = 0;
    if (!_spineItems.empty()) {
        for (const auto& s : _spineItems) {
            if (s.byteOffset > offset) break;
            idx = s.spineIndex;
            outStart = s.byteOffset;
        }
    } else {
        for (size_t i = 0; i < _chapters.size(); i++) {
            if (_chapters[i].byteOffset > offset) break;
            idx = (int)i;
            outStart = _chapters[i].byteOffset;
        }
    }
    return idx;
}

float SimpleReader::getProgress() const {
    if (_fileSize == 0 || _pageOffsets.empty()) return 0.0f;
    return (float)((double)_pageOffsets[_currentPageIndex] / (double)_fileSize);
}

bool SimpleReader::setPageByProgress(float percent) {
    if (!_isOpened || _pageOffsets.empty() || _fileSize == 0) return false;
    if (percent <= 0.0f) return setPage(1);
    if (percent >= 1.0f) return setPage(getTotalPages());
    // Round to the nearest byte: (float)offset/size * size may land one byte
    // short of a page start and select the previous page.
    uint32_t targetOffset = (uint32_t)((double)percent * (double)_fileSize + 0.5);
    return setPage(offsetToPage(targetOffset));
}

String SimpleReader::getXPath() const {
    if (!_isOpened || _pageOffsets.empty() || _currentPageIndex < 0 || _currentPageIndex >= (int)_pageOffsets.size()) {
        return "/body/DocFragment[1]/body";
    }
    const uint32_t pageOffset = _pageOffsets[_currentPageIndex];
    uint32_t chapterStart = 0;
    const int spineIdx = spineItemAt(pageOffset, chapterStart);

    // Walk the chapter up to the page start: count paragraph breaks and the
    // characters since the last one.
    int paragraph = 1;
    size_t charsInParagraph = 0;
    if (pageOffset > chapterStart) {
        FsFile f = Storage.open(_textFilePath.c_str(), O_RDONLY);
        if (f && f.seek(chapterStart)) {
            uint8_t buf[512];
            uint32_t pos = chapterStart;
            char lastC = '\0';
            while (pos < pageOffset) {
                size_t want = pageOffset - pos;
                if (want > sizeof(buf)) want = sizeof(buf);
                int n = f.read(buf, want);
                if (n <= 0) break;
                for (int j = 0; j < n; j++) {
                    const char c = (char)buf[j];
                    if (c == '\n' && lastC == '\n') {
                        paragraph++;
                        charsInParagraph = 0;
                    } else if (c != '\n' && isUtf8Lead(buf[j])) {
                        charsInParagraph++;
                    }
                    lastC = c;
                }
                pos += n;
            }
        }
        if (f) f.close();
    }
    return KOReaderSyncMapper::buildXPath(spineIdx, paragraph, charsInParagraph);
}

bool SimpleReader::setPageByXPath(const String& xpath, float fallbackPercent) {
    if (!_isOpened || _pageOffsets.empty()) return false;

    ParsedXPath parsed = KOReaderSyncMapper::parseXPath(xpath);
    uint32_t target = 0;
    if (!parsed.isValid || parsed.spineIndex < 0 || !spineItemStart(parsed.spineIndex, target)) {
        Serial.printf("[KOReader] XPath '%s' not usable, using %.1f%%\n", xpath.c_str(), fallbackPercent * 100.0f);
        return setPageByProgress(fallbackPercent);
    }

    // Skip (paragraph - 1) paragraph breaks, then charOffset characters
    // (newlines are not counted, mirroring getXPath()).
    if (parsed.paragraphIndex > 1 || parsed.charOffset > 0) {
        FsFile f = Storage.open(_textFilePath.c_str(), O_RDONLY);
        if (f && f.seek(target)) {
            uint8_t buf[512];
            uint32_t pos = target;
            int paragraph = 1;
            size_t chars = 0;
            char lastC = '\0';
            bool done = false;
            while (!done) {
                int n = f.read(buf, sizeof(buf));
                if (n <= 0) break;
                for (int j = 0; j < n && !done; j++) {
                    const char c = (char)buf[j];
                    if (paragraph >= parsed.paragraphIndex && c != '\n' && isUtf8Lead(buf[j])) {
                        if (chars == parsed.charOffset) { target = pos; done = true; break; }
                        chars++;
                    }
                    if (c == '\n' && lastC == '\n' && paragraph < parsed.paragraphIndex) {
                        paragraph++;
                    }
                    lastC = c;
                    pos++;
                }
            }
            if (!done) target = pos;   // past the end: clamp
        }
        if (f) f.close();
    }

    const int page = offsetToPage(target);
    if (_fileSize > 0 && ProgressSync::hasProgress(fallbackPercent)) {
        const float xpathPercent = (float)((double)target / (double)_fileSize);
        if (fabsf(xpathPercent - fallbackPercent) > XPATH_TRUST_TOLERANCE) {
            Serial.printf("[KOReader] XPath '%s' points to %.1f%% but progress is %.1f%%: using the percentage\n",
                          xpath.c_str(), xpathPercent * 100.0f, fallbackPercent * 100.0f);
            return setPageByProgress(fallbackPercent);
        }
    }
    Serial.printf("[KOReader] setPageByXPath: '%s' -> offset %u -> page %d of %d\n",
                  xpath.c_str(), (unsigned)target, page, getTotalPages());
    return setPage(page);
}

// ---------------------------------------------------------------------------
// EPUB CFI
// ---------------------------------------------------------------------------
constexpr float CFI_TRUST_TOLERANCE = 0.03f;   // the web reader's percent scale differs by up to ~1%

const EpubExtractor::CfiChapter* SimpleReader::cfiChapter(int spineIndex, int ordinal) {
    if (!EpubExtractor::isEpub(_originalFilePath)) return nullptr;
    if (!_cfiMapLoaded) {
        _cfiMapLoaded = true;
        EpubExtractor::loadCfiMap(_originalFilePath, _cfiMap);
    }
    for (const auto& c : _cfiMap) {
        if ((spineIndex >= 0 && c.spineIndex == spineIndex) || (ordinal > 0 && c.ordinal == ordinal)) return &c;
    }
    return nullptr;
}

bool SimpleReader::setPageByCfi(const String& cfi, float fallbackPercent) {
    if (!_isOpened || _pageOffsets.empty() || cfi.length() == 0) return false;
    EpubCfi::Location loc;
    if (!EpubCfi::parse(cfi.c_str(), loc)) {
        Serial.printf("[CFI] Cannot parse '%s'\n", cfi.c_str());
        return false;
    }
    const EpubExtractor::CfiChapter* ch = cfiChapter(-1, loc.ordinal);
    uint32_t start = 0;
    if (!ch || !spineItemStart(ch->spineIndex, start)) {
        Serial.printf("[CFI] '%s': spine item %d not in this book\n", cfi.c_str(), loc.ordinal);
        return false;
    }
    uint32_t inChapter = 0;
    if (!EpubExtractor::cfiToChapterOffset(_originalFilePath, ch->zipPath, loc, inChapter)) {
        Serial.printf("[CFI] '%s': cannot read %s\n", cfi.c_str(), ch->zipPath.c_str());
        return false;
    }
    uint32_t target = start + inChapter;
    // Never past the next chapter (the converted text ends each chapter with a page break).
    for (const auto& s : _spineItems) {
        if (s.byteOffset > start) { if (target >= s.byteOffset) target = s.byteOffset - 1; break; }
    }
    if (_fileSize > 0 && target >= _fileSize) target = _fileSize - 1;
    if (_fileSize > 0 && ProgressSync::hasProgress(fallbackPercent)) {
        const float cfiPercent = (float)((double)target / (double)_fileSize);
        if (fabsf(cfiPercent - fallbackPercent) > CFI_TRUST_TOLERANCE) {
            Serial.printf("[CFI] '%s' points to %.1f%% but progress is %.1f%%: stale, not used\n",
                          cfi.c_str(), cfiPercent * 100.0f, fallbackPercent * 100.0f);
            return false;
        }
    }
    const int page = offsetToPage(target);
    Serial.printf("[CFI] '%s' -> %s +%u -> offset %u -> page %d of %d\n", cfi.c_str(), ch->zipPath.c_str(),
                  (unsigned)inChapter, (unsigned)target, page, getTotalPages());
    return setPage(page);
}

String SimpleReader::getCfi() {
    if (!_isOpened || _pageOffsets.empty() || !EpubExtractor::isEpub(_originalFilePath)) return "";
    const uint32_t pageOffset = _pageOffsets[_currentPageIndex];
    if (pageOffset == _cfiCacheOffset) return _cfiCache;
    uint32_t start = 0;
    const int spineIdx = spineItemAt(pageOffset, start);
    const EpubExtractor::CfiChapter* ch = cfiChapter(spineIdx, -1);
    if (!ch || ch->ordinal <= 0) return "";
    EpubCfi::Location loc;
    if (!EpubExtractor::chapterOffsetToCfi(_originalFilePath, ch->zipPath, pageOffset - start, loc)) {
        // No text from here to the end of the item (a cover, an image page): its <body>.
        loc = EpubCfi::Location();
        loc.steps.push_back(4);
    }
    loc.ordinal = ch->ordinal;
    _cfiCache = EpubCfi::format(loc).c_str();
    _cfiCacheOffset = pageOffset;
    return _cfiCache;
}

bool SimpleReader::goToPosition(const String& cfi, const String& xpath, float percent, int page) {
    if (cfi.length() > 0 && setPageByCfi(cfi, percent)) return true;
    if (xpath.length() > 0) return setPageByXPath(xpath, percent);
    if (ProgressSync::hasProgress(percent)) return setPageByProgress(percent);
    if (page > 1) return setPage(page);
    return setPage(1);
}

void SimpleReader::drawStatusBar() {
    // Chrome around the page text (the text itself is laid out by
    // FontEngine::drawBookPage between y=42 and the bottom rule).
    using namespace uikit;
    Pass pass;
    Frame& f = pass.frame();
    const int16_t left = 20, right = (int16_t)(width() - 20);

    // Top: title on the left, "page/total | battery%" on the right.
    char statusRight[64];
    snprintf(statusRight, sizeof(statusRight), "%d/%d  |  %d%%", getCurrentPage(), getTotalPages(),
             Battery.getPercentage());
    const int16_t rw = (int16_t)(textWidth(FontSmall, statusRight) + 4);
    Rect topBand{left, 4, (int16_t)(right - left), 24};
    text(Rect{(int16_t)(right - rw), topBand.y, rw, topBand.height}, statusRight, FontSmall, Color::Black,
         TextAlign::Right);
    text(Rect{left, topBand.y, (int16_t)(topBand.width - rw - 12), topBand.height}, _bookTitle.c_str(), FontSmall);
    hline(left, right, 30);

    // Bottom: chapter name and the book progress bar.
    const int16_t bottomRule = (int16_t)(height() - 44);
    hline(left, right, bottomRule);
    String chTitle = getCurrentChapterTitle();
    const int32_t permille = (int32_t)(getProgress() * 1000.0f);
    if (chTitle.length() > 0) {
        text(Rect{left, (int16_t)(bottomRule + 2), (int16_t)(right - left), 20}, chTitle.c_str(), FontSmall);
        progress(f, Rect{left, (int16_t)(height() - 18), (int16_t)(right - left), 8}, permille);
    } else {
        progress(f, Rect{left, (int16_t)(height() - 28), (int16_t)(right - left), 10}, permille);
    }
}

void SimpleReader::renderCurrentPage() {
    if (!_isOpened || _pageOffsets.empty()) return;

    if (_currentPageIndex < 0) _currentPageIndex = 0;
    if (_currentPageIndex >= (int)_pageOffsets.size()) {
        _currentPageIndex = (int)_pageOffsets.size() - 1;
    }
    Serial.printf("[Reader] Page %d/%d\n", _currentPageIndex + 1, (int)_pageOffsets.size());

    Display.clear(EPD_WHITE);
    drawStatusBar();

    // Read page content
    FsFile f = Storage.open(_textFilePath.c_str(), O_RDONLY);
    if (!f) return;

    uint32_t startOffset = _pageOffsets[_currentPageIndex];
    uint32_t endOffset = (_currentPageIndex + 1 < (int)_pageOffsets.size()) 
                          ? _pageOffsets[_currentPageIndex + 1] 
                          : (uint32_t)_fileSize;

    bool startsWithParagraph = true;
    if (startOffset > 0) {
        f.seek(startOffset - 1);
        char prev = 0;
        f.read(&prev, 1);
        if (prev != '\n' && prev != '\r' && prev != '\f') {
            startsWithParagraph = false;
        }
    }

    f.seek(startOffset);
    size_t bytesToRead = (endOffset > startOffset) ? (endOffset - startOffset) : 0;
    if (bytesToRead > 4096) bytesToRead = 4096;

    if (bytesToRead > 0) {
        char* pageBuf = (char*)malloc(bytesToRead + 1);
        if (pageBuf) {
            size_t readCount = f.readBytes(pageBuf, bytesToRead);
            pageBuf[readCount] = '\0';

            // Render page with formatted typography (headers, bold, red-line indents)
            const BookPageGeometry geo = bookPageGeometry();
            Font.drawBookPage(geo.left, geo.top, pageBuf, geo.width, startsWithParagraph, _fontSize);
            free(pageBuf);
        }
    }
    f.close();

    // Refresh e-ink (Full refresh every N pages to eliminate ghosting, otherwise fast partial)
    if (_currentPageIndex % FULL_REFRESH_EVERY_PAGES == 0) {
        Display.update(REFRESH_FULL);
    } else {
        Display.update(REFRESH_PARTIAL);
    }
}
