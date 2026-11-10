#include "fb2_to_epub.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <new>
#include <vector>

#include <epub/Expat.h>

#include "zip_writer.h"

namespace convert {

const char* resultName(ConvertResult r) {
    switch (r) {
        case ConvertResult::Ok: return "ok";
        case ConvertResult::NotThisFormat: return "not this format";
        case ConvertResult::ParseError: return "parse error";
        case ConvertResult::IoError: return "card read/write error";
        case ConvertResult::NoMemory: return "no memory";
    }
    return "?";
}

namespace {

// ---------------------------------------------------------------- helpers

uint32_t hashId(const char* s, size_t n) {   // FNV-1a
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h ? h : 1;
}
uint32_t hashId(const char* s) { return hashId(s, strlen(s)); }

const char* localName(const char* name) {
    const char* c = strrchr(name, ':');
    return c ? c + 1 : name;
}

const char* attr(const char** atts, const char* local) {
    for (int i = 0; atts && atts[i]; i += 2)
        if (strcmp(localName(atts[i]), local) == 0) return atts[i + 1];
    return nullptr;
}

// Appends with whitespace collapsed, up to `cap` bytes.
void appendCollapsed(std::string& out, const char* s, int len, size_t cap) {
    for (int i = 0; i < len && out.size() < cap; i++) {
        const char c = s[i];
        const bool ws = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (ws) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += c;
        }
    }
}

// "CHAPTER I.", "Глава 3", "Part Two": a heading that only numbers what follows.
bool isChapterNumber(const std::string& t) {
    if (t.size() > 24) return false;
    static const char* const words[] = {"chapter", "part", "book", "\xD0\xB3\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0",
                                        "\xD0\x93\xD0\x9B\xD0\x90\xD0\x92\xD0\x90",
                                        "\xD0\x93\xD0\xBB\xD0\xB0\xD0\xB2\xD0\xB0",
                                        "\xD1\x87\xD0\xB0\xD1\x81\xD1\x82\xD1\x8C",
                                        "\xD0\xA7\xD0\xB0\xD1\x81\xD1\x82\xD1\x8C"};
    std::string l = t;
    for (char& c : l)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    for (const char* w : words) {
        const size_t n = strlen(w);
        if (l.compare(0, n, w) == 0 && (l.size() == n || l[n] == ' ')) return true;
    }
    return false;
}

void trim(std::string& s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    s.erase(0, i);
}

std::string xmlEscape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

// windows-1251, 0x80..0xFF (Russian FB2 files often come in it).
const uint16_t kCp1251[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C,
    0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0459, 0x203A,
    0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9,
    0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415,
    0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423,
    0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431,
    0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D,
    0x044E, 0x044F};

int XMLCALL unknownEncoding(void*, const XML_Char* name, XML_Encoding* info) {
    std::string n = name;
    for (char& c : n) c = (char)tolower((unsigned char)c);
    if (n != "windows-1251" && n != "cp1251" && n != "cp-1251" && n != "win-1251") return XML_STATUS_ERROR;
    for (int i = 0; i < 128; i++) info->map[i] = i;
    for (int i = 0; i < 128; i++) info->map[128 + i] = kCp1251[i];
    info->data = nullptr;
    info->convert = nullptr;
    info->release = nullptr;
    return XML_STATUS_OK;
}

enum E : uint8_t {
    E_OTHER, E_DESCRIPTION, E_TITLE_INFO, E_BOOK_TITLE, E_AUTHOR, E_FIRST, E_MIDDLE, E_LAST, E_NICK, E_LANG,
    E_SEQUENCE, E_COVERPAGE, E_ANNOTATION, E_BODY, E_SECTION, E_TITLE, E_EPIGRAPH, E_CITE, E_POEM, E_STANZA, E_V,
    E_P, E_SUBTITLE, E_TEXT_AUTHOR, E_DATE, E_EMPTY_LINE, E_IMAGE, E_A, E_EMPHASIS, E_STRONG, E_STRIKE, E_SUB,
    E_SUP, E_CODE, E_STYLE, E_TABLE, E_TR, E_TD, E_TH, E_BINARY
};

E elemOf(const char* name) {
    static const struct { const char* n; E e; } k[] = {
        {"description", E_DESCRIPTION}, {"title-info", E_TITLE_INFO}, {"book-title", E_BOOK_TITLE},
        {"author", E_AUTHOR}, {"first-name", E_FIRST}, {"middle-name", E_MIDDLE}, {"last-name", E_LAST},
        {"nickname", E_NICK}, {"lang", E_LANG}, {"sequence", E_SEQUENCE}, {"coverpage", E_COVERPAGE},
        {"annotation", E_ANNOTATION}, {"body", E_BODY}, {"section", E_SECTION}, {"title", E_TITLE},
        {"epigraph", E_EPIGRAPH}, {"cite", E_CITE}, {"poem", E_POEM}, {"stanza", E_STANZA}, {"v", E_V},
        {"p", E_P}, {"subtitle", E_SUBTITLE}, {"text-author", E_TEXT_AUTHOR}, {"date", E_DATE},
        {"empty-line", E_EMPTY_LINE}, {"image", E_IMAGE}, {"a", E_A}, {"emphasis", E_EMPHASIS},
        {"strong", E_STRONG}, {"strikethrough", E_STRIKE}, {"sub", E_SUB}, {"sup", E_SUP}, {"code", E_CODE},
        {"style", E_STYLE}, {"table", E_TABLE}, {"tr", E_TR}, {"td", E_TD}, {"th", E_TH}, {"binary", E_BINARY},
    };
    const char* l = localName(name);
    for (const auto& x : k)
        if (strcmp(l, x.n) == 0) return x.e;
    return E_OTHER;
}

constexpr int kMaxDepth = 96;
constexpr uint32_t kSectionSplit = 24 * 1024;   // a new section starts a new file past this
constexpr uint32_t kHardSplit = 64 * 1024;      // any paragraph does past this
constexpr uint32_t kElementWeight = 16;
constexpr size_t kMaxIds = 8192;

const char* const kImageMime[] = {"image/jpeg", "image/png", "image/gif"};

uint8_t imageType(const char* contentType) {
    if (contentType && strstr(contentType, "png")) return 1;
    if (contentType && strstr(contentType, "gif")) return 2;
    return 0;
}

// base64 value of a byte, -1 for anything else (whitespace, '=').
int8_t g_b64[256];
bool g_b64Ready = false;
void b64Init() {
    memset(g_b64, -1, sizeof(g_b64));
    for (int i = 0; i < 26; i++) g_b64['A' + i] = (int8_t)i, g_b64['a' + i] = (int8_t)(26 + i);
    for (int i = 0; i < 10; i++) g_b64['0' + i] = (int8_t)(52 + i);
    g_b64[(uint8_t)'+'] = 62;
    g_b64[(uint8_t)'/'] = 63;
    g_b64Ready = true;
}

const char* const kCss =
    "body{margin:0}\n"
    "p{margin:0;text-indent:1.5em;text-align:justify}\n"
    "h1,h2,h3,h4,h5,h6{text-align:center;text-indent:0;margin:1em 0 0.5em 0;font-weight:bold}\n"
    "h1{font-size:1.5em}h2{font-size:1.3em}h3{font-size:1.15em}h4,h5,h6{font-size:1em}\n"
    "p.subtitle{text-align:center;text-indent:0;font-weight:bold;margin:0.7em 0 0.3em 0}\n"
    "p.empty{text-indent:0}\n"
    "blockquote{margin:0.5em 0 0.5em 2em}\n"
    "blockquote.epigraph p{font-style:italic;text-indent:0}\n"
    "p.text-author{text-align:right;text-indent:0;font-style:italic}\n"
    "div.poem{margin:0.5em 0 0.5em 2em}\n"
    "div.stanza{margin:0 0 0.7em 0}\n"
    "p.v{text-indent:0;text-align:left}\n"
    "p.poem-title{text-indent:0;font-weight:bold}\n"
    "p.date{text-align:right;text-indent:0}\n"
    "div.image{text-align:center;text-indent:0;margin:0.5em 0}\n"
    "div.annotation{font-style:italic}\n"
    "td,th{padding:0.2em}\n";

// What an open element closes with in the output.
enum Close : uint8_t {
    C_NONE, C_P, C_H, C_BLOCKQUOTE, C_DIV, C_EM, C_STRONG, C_DEL, C_SUB, C_SUP, C_CODE, C_SPAN, C_A, C_TABLE, C_TR,
    C_TD, C_TH
};

struct TocOpen {
    std::string anchor;
    uint16_t file = 0;
    bool labelled = false;
};

// ------------------------------------------------------------------ converter

class Fb2Converter {
public:
    Fb2Converter(InFile& in, OutFile& out, TempFile& tmp, ProgressFn p, void* u, ConvertStats& s)
        : _in(in), _out(out), _tmp(tmp), _progress(p), _user(u), _stats(s) {}

    ConvertResult run();

    static void XMLCALL onStart(void* self, const XML_Char* name, const XML_Char** atts) {
        static_cast<Fb2Converter*>(self)->start(name, atts);
    }
    static void XMLCALL onEnd(void* self, const XML_Char* name) { static_cast<Fb2Converter*>(self)->end(name); }
    static void XMLCALL onText(void* self, const XML_Char* s, int len) {
        static_cast<Fb2Converter*>(self)->text(s, len);
    }

private:
    ConvertResult parsePass();
    void resetPass();
    void start(const char* name, const char** atts);
    void end(const char* name);
    void text(const char* s, int len);

    // description
    void descStart(E e, const char** atts);
    void descEnd(E e);

    // output
    void emit(const char* s) { emit(s, (uint32_t)strlen(s)); }
    void emit(const char* s, uint32_t n);
    void emitText(const char* s, int len);
    void emitAttrValue(const char* s);
    void flushOut();
    void ensureChapter();
    void closeChapter();
    void requestSplit(bool sectionStart, int sectionDepth);
    void beginBlock();   // a block start: split, chapter, pending blank lines
    void openTag(const char* tag, const char* cls, const char* ownId, bool takeAnchor);
    void anchorSpan();
    std::string hrefFor(const char* href);
    int imageRef(const char* href);           // pass 2: an image's number, given on first use
    void binaryStart(const char* id, const char* contentType);
    void binaryData(const char* s, int len);
    void binaryEnd();
    ConvertResult rawBinaries(uint64_t offset);

    // toc
    void tocLabel(int depth, const std::string& label);
    void tocFinishOpen(int depth);
    void tocRecord(int depth, uint16_t file, const std::string& anchor, const std::string& label);

    // finishing
    bool writeNcx();
    bool writeOpf();

    InFile& _in;
    OutFile& _out;
    TempFile& _tmp;
    ProgressFn _progress;
    void* _user;
    ConvertStats& _stats;
    XML_Parser _parser = nullptr;

    int _pass = 0;            // 1: survey, 2: output
    uint64_t _read = 0;
    int _lastPermille = -1;

    // element stack
    E _stack[kMaxDepth];
    Close _close[kMaxDepth];
    uint8_t _hLevel[kMaxDepth];
    int _depth = 0;
    int _skipDepth = 0;       // > 0: inside an element too deep to follow

    // description
    bool _inTitleInfo = false;
    int _descDepth = 0;
    std::string _descText;
    std::string _authorFirst, _authorMiddle, _authorLast, _authorNick;
    std::vector<std::string> _authors;
    std::string _bookTitle, _lang, _seriesName, _seriesNumber, _annotation, _coverHref;
    bool _inAnnotation = false;

    // bodies
    int _body = 0;            // bodies seen
    bool _notesBody = false;
    int _sectionDepth = 0;
    int _topSections = 0;     // top-level sections of the main body (pass 1)
    int _topSectionsSeen = 0;
    int _textDepth = 0;       // inside p / v / title...
    int _openBlocks = 0;      // inside blockquote / div / table
    int _titleDepth = 0;      // > 0: inside <title> (its level in _depth)
    int _titleLines = 0;
    bool _titleAsParas = false;
    std::string _titleText;
    std::string _firstParaText;
    std::string _heldLabel;
    std::vector<std::string> _fileLabels;   // first words of each file (pass 2): contents of last resort   // "CHAPTER I." waiting for the line that names it
    bool _collectFirstPara = false;
    int _pendingEmpty = 0;
    bool _spacerEmptyLines = false;   // empty-line used as paragraph spacing (calibre output)
    uint32_t _pCount = 0, _emptyCount = 0;
    std::string _pendingAnchor;
    uint32_t _genId = 0;

    // chapters
    uint16_t _file = 1;
    bool _fileHasContent = false;
    bool _chapterOpen = false;
    bool _splitWanted = false;
    uint32_t _weight = 0;
    uint16_t _maxFile = 0;
    char _buf[1024];
    uint32_t _bufLen = 0;
    ZipWriter _zip;

    // tables from pass 1
    std::vector<uint32_t> _idHash;
    std::vector<uint16_t> _idFile;
    std::vector<uint32_t> _imgHash;   // images in order of first reference (pass 2)
    std::vector<uint8_t> _imgType;
    bool _idsOverflow = false;

    std::vector<bool> _imgWritten;
    // binary decode (pass 2)
    bool _inBinary = false;
    bool _utf16 = false;          // binaries go through expat (no raw scan)
    bool _stopped = false;        // the parse stopped at the first <binary>
    uint64_t _binaryOffset = 0;
    uint32_t _b64 = 0;
    int _b64n = 0;
    uint8_t _bin[4096];
    uint8_t _raw[8192];   // raw image reads (sector multiples)
    uint32_t _binLen = 0;
    int _imageCount = 0;

    // toc
    TocOpen _toc[4];
    uint32_t _tocCount = 0;
    bool _tmpOk = true;
};

// ---------------------------------------------------------------------- run

ConvertResult Fb2Converter::run() {
    // Pass 1: survey.
    _pass = 1;
    ConvertResult r = parsePass();
    if (r != ConvertResult::Ok) return r;
    // Ids sorted for lookup (stable: the first of duplicated ids wins).
    {
        std::vector<uint32_t> order(_idHash.size());
        for (uint32_t i = 0; i < order.size(); i++) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return _idHash[a] < _idHash[b]; });
        std::vector<uint32_t> h(order.size());
        std::vector<uint16_t> f(order.size());
        for (uint32_t i = 0; i < order.size(); i++) {
            h[i] = _idHash[order[i]];
            f[i] = _idFile[order[i]];
        }
        _idHash.swap(h);
        _idFile.swap(f);
    }
    _stats.ids = (uint32_t)_idHash.size();
    _spacerEmptyLines = _pCount > 0 && _emptyCount * 10 >= _pCount * 6;
    _topSections = _topSectionsSeen;

    // Pass 2: output.
    _pass = 2;
    _imgHash.clear();
    _imgType.clear();
    _imgWritten.clear();
    if (!g_b64Ready) b64Init();
    _zip.begin(&_out);
    _zip.add("mimetype", "application/epub+zip");
    _zip.add("META-INF/container.xml",
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
             "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
             "</rootfiles></container>\n");
    _zip.add("OEBPS/style.css", kCss);
    _tmpOk = _tmp.beginWrite();
    r = parsePass();
    if (r != ConvertResult::Ok) return r;
    closeChapter();
    if (_maxFile == 0) {   // no text at all: one empty chapter keeps the EPUB valid
        _file = 1;
        _fileHasContent = true;
        ensureChapter();
        emit("<p>&#160;</p>");
        closeChapter();
    }
    if (!writeNcx() || !writeOpf() || !_zip.finish()) {
        _stats.error = "writing the EPUB failed";
        return ConvertResult::IoError;
    }
    _stats.chapters = _maxFile;
    _stats.images = (uint32_t)_imageCount;
    _stats.tocEntries = _tocCount;
    _stats.title = _bookTitle;
    _stats.lang = _lang;
    for (size_t i = 0; i < _authors.size(); i++) _stats.author += (i ? ", " : "") + _authors[i];
    if (_progress) _progress(_user, 1000);
    return ConvertResult::Ok;
}

void Fb2Converter::resetPass() {
    _depth = 0;
    _skipDepth = 0;
    _inTitleInfo = false;
    _descDepth = 0;
    _body = 0;
    _notesBody = false;
    _sectionDepth = 0;
    _textDepth = 0;
    _openBlocks = 0;
    _titleDepth = 0;
    _pendingEmpty = 0;
    _pendingAnchor.clear();
    _genId = 0;
    _file = 1;
    _fileHasContent = false;
    _chapterOpen = false;
    _splitWanted = false;
    _weight = 0;
    _maxFile = 0;
    _bufLen = 0;
    _inBinary = false;
    _stopped = false;
    _imageCount = 0;
    _topSectionsSeen = 0;
    _collectFirstPara = false;
    for (auto& t : _toc) t = TocOpen();
    _tocCount = 0;
    _read = 0;
}

ConvertResult Fb2Converter::parsePass() {
    resetPass();
    if (!_in.rewind()) {
        _stats.error = "cannot read the book";
        return ConvertResult::IoError;
    }
    _parser = XML_ParserCreate(nullptr);
    if (!_parser) return ConvertResult::NoMemory;
    XML_SetUserData(_parser, this);
    XML_SetElementHandler(_parser, onStart, onEnd);
    XML_SetCharacterDataHandler(_parser, onText);
    XML_SetUnknownEncodingHandler(_parser, unknownEncoding, nullptr);
    ConvertResult r = ConvertResult::Ok;
    const uint64_t total = _in.size() ? _in.size() : 1;
    bool first = true;
    for (;;) {
        void* buf = XML_GetBuffer(_parser, 8192);
        if (!buf) {
            r = ConvertResult::NoMemory;
            break;
        }
        const int32_t n = _in.read(buf, 8192);
        if (n < 0) {
            _stats.error = "cannot read the book";
            r = ConvertResult::IoError;
            break;
        }
        if (first && n > 0) {
            // A FictionBook says so near the top; anything else is not ours.
            const char* b = static_cast<const char*>(buf);
            const uint8_t* u = static_cast<const uint8_t*>(buf);
            _utf16 = n >= 2 && ((u[0] == 0xFF && u[1] == 0xFE) || (u[0] == 0xFE && u[1] == 0xFF) || !u[0] || !u[1]);
            bool fb = _utf16;   // (its name is in 16-bit characters there: expat decides)
            for (int32_t i = 0; i + 11 <= n && !fb; i++) fb = memcmp(b + i, "FictionBook", 11) == 0;
            if (!fb) {
                r = ConvertResult::NotThisFormat;
                break;
            }
            first = false;
        }
        if (XML_ParseBuffer(_parser, n, n == 0) == XML_STATUS_ERROR && !_stopped) {
            char msg[96];
            snprintf(msg, sizeof(msg), "XML error at line %lu: %s", (unsigned long)XML_GetCurrentLineNumber(_parser),
                     XML_ErrorString(XML_GetErrorCode(_parser)));
            _stats.error = msg;
            r = ConvertResult::ParseError;
            break;
        }
        _read += (uint64_t)n;
        if (_progress) {
            const int pm = _pass == 1 ? (int)(100 * _read / total) : 100 + (int)(900 * _read / total);
            if (pm / 10 != _lastPermille / 10) {
                _lastPermille = pm;
                _progress(_user, pm);
            }
        }
        if (_pass == 2 && _zip.failed()) {
            _stats.error = "writing the EPUB failed";
            r = ConvertResult::IoError;
            break;
        }
        if (n == 0 || _stopped) break;
    }
    XML_ParserFree(_parser);
    _parser = nullptr;
    if (r == ConvertResult::Ok && _pass == 2 && _stopped) {
        r = rawBinaries(_binaryOffset);
        if (r != ConvertResult::Ok) _stats.error = "reading the images failed";
    }
    if (r == ConvertResult::Ok && _pass == 2 && _zip.failed()) r = ConvertResult::IoError;
    return r;
}

// --------------------------------------------------------------- description

void Fb2Converter::descStart(E e, const char** atts) {
    if (e == E_TITLE_INFO) _inTitleInfo = true;
    if (!_inTitleInfo) return;
    if (e == E_ANNOTATION) _inAnnotation = true;
    if (!_inAnnotation) _descText.clear();
    if (e == E_AUTHOR) _authorFirst.clear(), _authorMiddle.clear(), _authorLast.clear(), _authorNick.clear();
    if (e == E_SEQUENCE && _seriesName.empty()) {
        if (const char* n = attr(atts, "name")) _seriesName = n;
        if (const char* v = attr(atts, "number")) _seriesNumber = v;
    }
    if (e == E_IMAGE && _depth >= 2 && _stack[_depth - 2] == E_COVERPAGE) {
        if (const char* h = attr(atts, "href")) {
            if (_coverHref.empty()) _coverHref = h;
            if (_coverHref == h) imageRef(h);   // the cover first: number 0
        }
    }
}

void Fb2Converter::descEnd(E e) {
    if (e == E_TITLE_INFO) {
        _inTitleInfo = false;
        return;
    }
    if (!_inTitleInfo) return;
    std::string t = _descText;
    trim(t);
    switch (e) {
        case E_BOOK_TITLE: if (_bookTitle.empty()) _bookTitle = t; break;
        case E_FIRST: _authorFirst = t; break;
        case E_MIDDLE: _authorMiddle = t; break;
        case E_LAST: _authorLast = t; break;
        case E_NICK: _authorNick = t; break;
        case E_LANG: if (_lang.empty()) _lang = t; break;
        case E_AUTHOR: {
            std::string a = _authorFirst;
            if (!_authorMiddle.empty()) a += (a.empty() ? "" : " ") + _authorMiddle;
            if (!_authorLast.empty()) a += (a.empty() ? "" : " ") + _authorLast;
            if (a.empty()) a = _authorNick;
            if (!a.empty() && _authors.size() < 8) _authors.push_back(a);
            break;
        }
        case E_ANNOTATION:
            if (_annotation.empty()) _annotation = t;
            _inAnnotation = false;
            break;
        default: break;
    }
    if (_inAnnotation) {
        if (e == E_P && !_descText.empty() && _descText.back() != ' ') _descText += ' ';
    } else if (e != E_AUTHOR) {
        _descText.clear();
    }
}

// --------------------------------------------------------------------- output

void Fb2Converter::flushOut() {
    if (_bufLen && _pass == 2) _zip.write(_buf, _bufLen);
    _bufLen = 0;
}

void Fb2Converter::emit(const char* s, uint32_t n) {
    if (_pass != 2) return;
    ensureChapter();
    while (n) {
        const uint32_t k = std::min<uint32_t>(n, sizeof(_buf) - _bufLen);
        memcpy(_buf + _bufLen, s, k);
        _bufLen += k;
        s += k;
        n -= k;
        if (_bufLen == sizeof(_buf)) flushOut();
    }
}

void Fb2Converter::emitText(const char* s, int len) {
    if (_pass != 2) return;
    int from = 0;
    for (int i = 0; i < len; i++) {
        const char* rep = nullptr;
        if (s[i] == '&') rep = "&amp;";
        else if (s[i] == '<') rep = "&lt;";
        else if (s[i] == '>') rep = "&gt;";
        if (rep) {
            emit(s + from, (uint32_t)(i - from));
            emit(rep);
            from = i + 1;
        }
    }
    emit(s + from, (uint32_t)(len - from));
}

void Fb2Converter::emitAttrValue(const char* s) {
    if (_pass != 2) return;
    const std::string e = xmlEscape(s);
    emit(e.data(), (uint32_t)e.size());
}

void Fb2Converter::ensureChapter() {
    if (_chapterOpen || _pass != 2) return;
    char name[32];
    snprintf(name, sizeof(name), "OEBPS/c%04u.xhtml", (unsigned)_file);
    _zip.open(name);
    _chapterOpen = true;
    std::string head =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"" + xmlEscape(_lang.empty() ? "en" : _lang) +
        "\"><head><title>" + xmlEscape(_bookTitle) +
        "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head><body>\n";
    emit(head.data(), (uint32_t)head.size());
}

void Fb2Converter::closeChapter() {
    if (_chapterOpen) {
        emit("\n</body></html>\n");
        flushOut();
        _zip.close();
        _chapterOpen = false;
    }
    if (_fileHasContent && _file > _maxFile) _maxFile = _file;
}

// A section start may begin a new file; past kHardSplit any block may.
void Fb2Converter::requestSplit(bool sectionStart, int sectionDepth) {
    if (!_fileHasContent) return;
    if (sectionStart && ((sectionDepth == 1 && !_notesBody) || _weight > kSectionSplit)) _splitWanted = true;
}

void Fb2Converter::beginBlock() {
    if (_openBlocks == 0 && _textDepth == 0 && _fileHasContent && (_splitWanted || _weight > kHardSplit)) {
        closeChapter();
        _file++;
        _fileHasContent = false;
        _weight = 0;
        _pendingEmpty = 0;   // blank lines never open a file
    }
    _splitWanted = false;
    // Blank lines held back: in calibre-style books an empty-line between every
    // two paragraphs is spacing, kept only where two or more stand together.
    const int blanks = _spacerEmptyLines ? (_pendingEmpty >= 2 ? 1 : 0) : std::min(_pendingEmpty, 3);
    _pendingEmpty = 0;
    for (int i = 0; i < blanks; i++) emit("<p class=\"empty\">&#160;</p>\n");
    _fileHasContent = true;
    if (_pass == 2 && _file <= _fileLabels.size()) {   // the words of two paragraphs stay apart
        std::string& l = _fileLabels[_file - 1];
        if (!l.empty() && l.back() != ' ') l += ' ';
    }
}

void Fb2Converter::openTag(const char* tag, const char* cls, const char* ownId, bool takeAnchor) {
    if (_pass != 2) {
        if (!ownId && takeAnchor) _pendingAnchor.clear();
        return;
    }
    emit("<");
    emit(tag);
    if (cls) {
        emit(" class=\"");
        emit(cls);
        emit("\"");
    }
    const char* id = ownId;
    if (!id && takeAnchor && !_pendingAnchor.empty()) id = _pendingAnchor.c_str();
    if (id) {
        emit(" id=\"");
        emitAttrValue(id);
        emit("\"");
    }
    emit(">");
    if (id == _pendingAnchor.c_str()) _pendingAnchor.clear();
}

// An anchor that could not go on the block itself (it has its own id).
void Fb2Converter::anchorSpan() {
    if (_pendingAnchor.empty()) return;
    if (_pass == 2) {
        emit("<span id=\"");
        emitAttrValue(_pendingAnchor.c_str());
        emit("\"></span>");
    }
    _pendingAnchor.clear();
}

std::string Fb2Converter::hrefFor(const char* href) {
    if (href[0] != '#') return href;
    const uint32_t h = hashId(href + 1);
    auto it = std::lower_bound(_idHash.begin(), _idHash.end(), h);
    if (it == _idHash.end() || *it != h) return href;
    const uint16_t f = _idFile[it - _idHash.begin()];
    if (f == _file) return href;
    char name[16];
    snprintf(name, sizeof(name), "c%04u.xhtml", (unsigned)f);
    return std::string(name) + href;
}

int Fb2Converter::imageRef(const char* href) {
    if (_pass != 2 || !href || href[0] != '#' || !href[1]) return -1;
    const uint32_t h = hashId(href + 1);
    for (size_t i = 0; i < _imgHash.size(); i++)
        if (_imgHash[i] == h) return (int)i;
    if (_imgHash.size() >= 4096) return -1;
    _imgHash.push_back(h);
    _imgType.push_back(0);
    _imgWritten.push_back(false);
    return (int)_imgHash.size() - 1;
}

// Images are named by number, without an extension: the engine reads the
// format from the data, the manifest from the content-type.
void Fb2Converter::binaryStart(const char* id, const char* contentType) {
    if (!id || !*id) return;
    const uint32_t h = hashId(id);
    int idx = -1;
    for (size_t i = 0; i < _imgHash.size(); i++)
        if (_imgHash[i] == h) idx = (int)i;
    if (idx < 0 || _imgWritten[idx]) return;   // never shown, or a duplicate id
    _imgWritten[idx] = true;
    _imgType[idx] = imageType(contentType);
    char nm[32];
    snprintf(nm, sizeof(nm), "OEBPS/images/i%04d", idx);
    _zip.open(nm);
    _inBinary = true;
    _b64 = 0;
    _b64n = 0;
    _binLen = 0;
    _imageCount++;
}

void Fb2Converter::binaryData(const char* s, int len) {
    if (!_inBinary) return;
    for (int i = 0; i < len; i++) {
        const int v = g_b64[(uint8_t)s[i]];
        if (v < 0) continue;   // whitespace, '=' padding
        _b64 = (_b64 << 6) | (uint32_t)v;
        if (++_b64n == 4) {
            _bin[_binLen++] = (uint8_t)(_b64 >> 16);
            _bin[_binLen++] = (uint8_t)(_b64 >> 8);
            _bin[_binLen++] = (uint8_t)_b64;
            _b64n = 0;
            _b64 = 0;
            if (_binLen + 5 > sizeof(_bin)) {   // room for the padded tail too
                _zip.write(_bin, _binLen);
                _binLen = 0;
            }
        }
    }
}

void Fb2Converter::binaryEnd() {
    if (!_inBinary) return;
    // A last group of 2 or 3 characters ('=' padded) holds 1 or 2 bytes.
    if (_b64n == 2) _bin[_binLen++] = (uint8_t)(_b64 >> 4);
    if (_b64n == 3) {
        _bin[_binLen++] = (uint8_t)(_b64 >> 10);
        _bin[_binLen++] = (uint8_t)(_b64 >> 2);
    }
    if (_binLen) _zip.write(_bin, _binLen);
    _zip.close();
    _inBinary = false;
}

// After the bodies a FictionBook holds only <binary> elements: megabytes of
// base64 that expat would tokenize character by character. They are read
// raw from the first one on: find a tag, take id and content-type, decode
// to the next '<'.
ConvertResult Fb2Converter::rawBinaries(uint64_t offset) {
    if (!_in.seek(offset)) return ConvertResult::IoError;
    _read = offset;
    const uint64_t total = _in.size() ? _in.size() : 1;
    enum { TEXT, TAG, DATA } st = TEXT;
    std::string tag;
    char* buf = reinterpret_cast<char*>(_raw);
    for (;;) {
        // Reads end on sector boundaries: whole sectors go straight to the buffer.
        const uint32_t want = (uint32_t)sizeof(_raw) - (uint32_t)(_read % 512);
        const int32_t n = _in.read(buf, want);
        if (n < 0) return ConvertResult::IoError;
        if (n == 0) break;
        _read += (uint64_t)n;
        int32_t i = 0;
        while (i < n) {
            if (st == TEXT || st == DATA) {
                const char* lt = static_cast<const char*>(memchr(buf + i, '<', (size_t)(n - i)));
                const int32_t end = lt ? (int32_t)(lt - buf) : n;
                if (st == DATA) binaryData(buf + i, end - i);
                if (!lt) {
                    i = n;
                    break;
                }
                if (st == DATA) binaryEnd();
                i = end + 1;
                tag.clear();
                st = TAG;
            } else {
                const char c = buf[i++];
                if (c != '>') {
                    if (tag.size() < 1024) tag += c;
                    continue;
                }
                st = TEXT;
                if (tag.compare(0, 6, "binary") != 0 || (tag.size() > 6 && tag[6] != ' ' && tag[6] != '\t' &&
                                                          tag[6] != '\n' && tag[6] != '\r'))
                    continue;
                auto value = [&](const char* name) -> std::string {
                    for (size_t at = 0; (at = tag.find(name, at)) != std::string::npos; at++) {
                        const char before = at ? tag[at - 1] : ' ';
                        const size_t q = at + strlen(name);
                        if ((before == ' ' || before == '\t' || before == '\n' || before == '\r') && q < tag.size() &&
                            (tag[q] == '"' || tag[q] == '\'')) {
                            const size_t close = tag.find(tag[q], q + 1);
                            if (close != std::string::npos) return tag.substr(q + 1, close - q - 1);
                        }
                    }
                    return std::string();
                };
                binaryStart(value("id=").c_str(), value("content-type=").c_str());
                st = DATA;
            }
        }
        if (_progress) {
            const int pm = 100 + (int)(900 * _read / total);
            if (pm / 10 != _lastPermille / 10) {
                _lastPermille = pm;
                _progress(_user, pm);
            }
        }
        if (_zip.failed()) return ConvertResult::IoError;
    }
    binaryEnd();
    return ConvertResult::Ok;
}

// ------------------------------------------------------------------------ toc

void Fb2Converter::tocRecord(int depth, uint16_t file, const std::string& anchor, const std::string& label) {
    _tocCount++;
    if (_pass != 2 || !_tmpOk) return;
    uint8_t head[3] = {(uint8_t)depth, (uint8_t)file, (uint8_t)(file >> 8)};
    const uint8_t al = (uint8_t)std::min<size_t>(anchor.size(), 255);
    const uint16_t ll = (uint16_t)std::min<size_t>(label.size(), 600);
    const uint8_t lb[2] = {(uint8_t)ll, (uint8_t)(ll >> 8)};
    _tmpOk = _tmp.write(head, 3) && _tmp.write(&al, 1) && _tmp.write(anchor.data(), al) && _tmp.write(lb, 2) &&
             _tmp.write(label.data(), ll);
}

void Fb2Converter::tocLabel(int depth, const std::string& label) {
    if (depth < 1 || depth > 3) return;
    TocOpen& t = _toc[depth];
    if (t.anchor.empty() || t.labelled) return;
    std::string l = label;
    trim(l);
    if (l.empty()) return;
    t.labelled = true;
    tocRecord(depth, t.file, t.anchor, l);
}

// An open TOC entry that got no title: a top-level section of a book with
// several gets its number; deeper untitled sections stay out.
void Fb2Converter::tocFinishOpen(int depth) {
    if (depth < 1 || depth > 3) return;
    TocOpen& t = _toc[depth];
    // (An empty section never placed its anchor: nothing to point at.)
    if (!t.anchor.empty() && !t.labelled && depth == 1 && _topSections > 1 && _pendingAnchor != t.anchor) {
        t.labelled = true;
        tocRecord(depth, t.file, t.anchor, _heldLabel.empty() ? std::to_string(_topSectionsSeen) : _heldLabel);
    }
    if (depth == 1) _heldLabel.clear();
    t.labelled = true;
}

// ---------------------------------------------------------------- SAX events

void Fb2Converter::start(const char* name, const char** atts) {
    if (_depth >= kMaxDepth) {
        _skipDepth++;
        return;
    }
    const E e = elemOf(name);
    const int parentDepth = _depth;
    const E parent = parentDepth > 0 ? _stack[parentDepth - 1] : E_OTHER;
    _stack[_depth] = e;
    _close[_depth] = C_NONE;
    _depth++;
    _weight += kElementWeight;

    if (e == E_DESCRIPTION || _descDepth > 0) {
        _descDepth++;
        descStart(e, atts);
        return;
    }
    if (e == E_BINARY) {
        // The bodies are over. Pass 1 has all it needs; pass 2 reads the images raw
        // (or through expat for a UTF-16 file, where a raw scan cannot find tags).
        closeChapter();
        if (_pass == 1 || !_utf16) {
            _binaryOffset = (uint64_t)XML_GetCurrentByteIndex(_parser);
            _stopped = true;
            XML_StopParser(_parser, XML_FALSE);
            return;
        }
        binaryStart(attr(atts, "id"), attr(atts, "content-type"));
        return;
    }
    if (e == E_BODY) {
        closeChapter();
        if (_fileHasContent) {
            _file++;
            _fileHasContent = false;
            _weight = 0;
        }
        _body++;
        const char* n = attr(atts, "name");
        _notesBody = _body > 1 && n && (strcmp(n, "notes") == 0 || strcmp(n, "comments") == 0);
        _sectionDepth = 0;
        if (_notesBody) {
            // The notes as one entry of the contents.
            _toc[1] = TocOpen();
            _toc[1].anchor = "fb2notes" + std::to_string(_body);
            _toc[1].file = _file;
            _pendingAnchor = _toc[1].anchor;
        }
        return;
    }
    if (_body == 0) return;   // outside description, bodies and binaries: nothing to show

    const char* ownId = attr(atts, "id");
    if (ownId && _pass == 1) {
        if (_idHash.size() < kMaxIds) {
            _idHash.push_back(hashId(ownId));
            _idFile.push_back(_file);   // corrected below for blocks that open a new file
        } else {
            _idsOverflow = true;
        }
    }
    // Record ids against the file the block lands in: decided by beginBlock().
    auto fixId = [&]() {
        if (ownId && _pass == 1 && !_idsOverflow && !_idFile.empty()) _idFile.back() = _file;
    };

    switch (e) {
        case E_SECTION: {
            _sectionDepth++;
            if (_sectionDepth == 1 && !_notesBody && _body == 1) _topSectionsSeen++;
            requestSplit(true, _sectionDepth);
            if (_splitWanted && _openBlocks == 0 && _textDepth == 0) {
                // Decide the file now so the contents entry points at it.
                closeChapter();
                _file++;
                _fileHasContent = false;
                _weight = 0;
                _splitWanted = false;
                _pendingEmpty = 0;
            }
            fixId();
            // Parents without a title so far get their fallback label before a child.
            for (int d = 1; d < _sectionDepth && d <= 3; d++)
                if (!_toc[d].labelled) tocFinishOpen(d);
            if (!_notesBody && _sectionDepth <= 3) {
                TocOpen& t = _toc[_sectionDepth];
                t = TocOpen();
                t.anchor = ownId ? ownId : "fb2s" + std::to_string(++_genId);
                t.file = _file;
                _pendingAnchor = t.anchor;
                _collectFirstPara = _sectionDepth == 1;
                _firstParaText.clear();
            } else if (ownId) {
                _pendingAnchor = ownId;
            }
            return;
        }
        case E_TITLE: {
            beginBlock();
            fixId();
            _titleDepth = _depth;
            _titleLines = 0;
            _titleText.clear();
            _textDepth++;
            _titleAsParas = parent == E_POEM || parent == E_STANZA;
            if (_titleAsParas) return;   // its lines become p.poem-title
            int level = parent == E_BODY ? 1 : std::min(_sectionDepth + 1, 6);
            if (_notesBody) level = parent == E_BODY ? 2 : 4;
            char tag[4] = {'h', (char)('0' + level), 0, 0};
            _hLevel[_depth - 1] = (uint8_t)level;
            openTag(tag, "title", ownId, true);
            anchorSpan();
            _close[_depth - 1] = C_H;
            return;
        }
        case E_EPIGRAPH:
        case E_CITE:
        case E_POEM:
        case E_STANZA:
        case E_ANNOTATION:
        case E_TABLE: {
            beginBlock();
            fixId();
            const bool quote = e == E_EPIGRAPH || e == E_CITE;
            const char* tag = quote ? "blockquote" : e == E_TABLE ? "table" : "div";
            const char* cls = e == E_EPIGRAPH ? "epigraph" : e == E_CITE ? "cite" : e == E_POEM ? "poem"
                            : e == E_STANZA ? "stanza" : e == E_ANNOTATION ? "annotation" : nullptr;
            openTag(tag, cls, ownId, true);
            anchorSpan();
            _close[_depth - 1] = quote ? C_BLOCKQUOTE : e == E_TABLE ? C_TABLE : C_DIV;
            _openBlocks++;
            return;
        }
        case E_TR:
            if (_textDepth == 0) {
                emit("<tr>");
                _close[_depth - 1] = C_TR;
            }
            return;
        case E_TD:
        case E_TH: {
            if (_textDepth) return;
            emit(e == E_TD ? "<td" : "<th");
            for (const char* a : {"colspan", "rowspan", "align"}) {
                if (const char* v = attr(atts, a)) {
                    emit(" ");
                    emit(a);
                    emit("=\"");
                    emitAttrValue(v);
                    emit("\"");
                }
            }
            emit(">");
            _close[_depth - 1] = e == E_TD ? C_TD : C_TH;
            _textDepth++;
            return;
        }
        case E_P:
        case E_V:
        case E_SUBTITLE:
        case E_TEXT_AUTHOR:
        case E_DATE: {
            if (_titleDepth) {
                // A line of a title.
                if (_titleAsParas) {
                    openTag("p", "poem-title", ownId, true);
                    _close[_depth - 1] = C_P;
                } else if (_titleLines++ > 0) {
                    emit("<br/>");
                    _titleText += ' ';
                }
                return;
            }
            if (_textDepth) return;   // nested in another text block: its text only
            if (e == E_P) _pCount++;
            beginBlock();
            fixId();
            const char* cls = e == E_V ? "v" : e == E_SUBTITLE ? "subtitle" : e == E_TEXT_AUTHOR ? "text-author"
                            : e == E_DATE ? "date" : nullptr;
            openTag("p", cls, ownId, true);
            anchorSpan();
            _close[_depth - 1] = C_P;
            _textDepth++;
            if (e == E_P && _collectFirstPara) _firstParaText.clear();
            return;
        }
        case E_EMPTY_LINE:
            if (_titleDepth) {
                emit("<br/>");
                return;
            }
            _emptyCount++;
            if (_textDepth == 0) _pendingEmpty++;
            return;
        case E_IMAGE: {
            const int idx = imageRef(attr(atts, "href"));
            const bool inlineImg = _textDepth > 0;
            if (!inlineImg) {
                beginBlock();
                fixId();
            }
            if (idx < 0 || _pass != 2) return;
            char src[40];
            snprintf(src, sizeof(src), "images/i%04d", idx);
            if (!inlineImg) {
                openTag("div", "image", ownId, true);
                anchorSpan();
            }
            emit("<img src=\"");
            emit(src);
            emit("\" alt=\"\"/>");
            if (!inlineImg) emit("</div>\n");
            return;
        }
        case E_A: {
            if (!_textDepth) return;
            const char* href = attr(atts, "href");
            const char* type = attr(atts, "type");
            emit("<a");
            if (type && strcmp(type, "note") == 0) emit(" class=\"note\"");
            if (href) {
                emit(" href=\"");
                emitAttrValue(hrefFor(href).c_str());
                emit("\"");
            }
            if (ownId) {
                emit(" id=\"");
                emitAttrValue(ownId);
                emit("\"");
            }
            emit(">");
            _close[_depth - 1] = C_A;
            return;
        }
        case E_EMPHASIS:
        case E_STRONG:
        case E_STRIKE:
        case E_SUB:
        case E_SUP:
        case E_CODE:
        case E_STYLE: {
            if (!_textDepth) return;
            static const char* const open[] = {"<em>", "<strong>", "<del>", "<sub>", "<sup>", "<code>", "<span>"};
            static const Close close[] = {C_EM, C_STRONG, C_DEL, C_SUB, C_SUP, C_CODE, C_SPAN};
            const int i = e - E_EMPHASIS;
            emit(open[i]);
            _close[_depth - 1] = close[i];
            return;
        }
        default:
            return;
    }
}

void Fb2Converter::end(const char*) {
    if (_skipDepth > 0) {
        _skipDepth--;
        return;
    }
    if (_depth == 0) return;
    _depth--;
    const E e = _stack[_depth];
    const Close c = _close[_depth];

    if (_descDepth > 0) {
        descEnd(e);
        _descDepth--;
        return;
    }
    if (e == E_BINARY) {
        binaryEnd();
        return;
    }
    if (e == E_BODY) {
        if (_notesBody && !_toc[1].labelled) {
            tocLabel(1, _lang.compare(0, 2, "ru") == 0 ? "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD1\x87\xD0\xB0"
                                                         "\xD0\xBD\xD0\xB8\xD1\x8F"
                                                       : "Notes");
        }
        _pendingEmpty = 0;
        _notesBody = false;
        return;
    }

    static const char* const closeText[] = {"", "</p>\n", "", "</blockquote>\n", "</div>\n", "</em>", "</strong>",
                                            "</del>", "</sub>", "</sup>", "</code>", "</span>", "</a>",
                                            "</table>\n", "</tr>\n", "</td>", "</th>"};
    if (c == C_H) {
        char tag[16];
        snprintf(tag, sizeof(tag), "</h%u>\n", (unsigned)_hLevel[_depth]);
        emit(tag);
    } else if (c != C_NONE) {
        emit(closeText[c]);
    }
    if (c == C_BLOCKQUOTE || c == C_DIV || c == C_TABLE) _openBlocks--;
    if (c == C_TD || c == C_TH) _textDepth--;

    switch (e) {
        case E_TITLE:
            if (_titleDepth == _depth + 1) {
                _titleDepth = 0;
                _textDepth--;
                trim(_titleText);
                if (_notesBody) {
                    if (_sectionDepth == 0) tocLabel(1, _titleText);
                } else if (_sectionDepth >= 1) {
                    tocLabel(_sectionDepth, _titleText);
                }
            }
            return;
        case E_P:
        case E_V:
        case E_SUBTITLE:
        case E_TEXT_AUTHOR:
        case E_DATE:
            if (c == C_P && !_titleDepth) {
                _textDepth--;
                if (e == E_P && _collectFirstPara) {
                    std::string t = _firstParaText;
                    trim(t);
                    if (!t.empty()) {
                        // A short first line of an untitled chapter names it ("CHAPTER I."),
                        // with the next one when it is only a number ("CHAPTER I. Down the
                        // Rabbit-Hole").
                        if (_heldLabel.empty() && isChapterNumber(t)) {
                            _heldLabel = t;
                        } else {
                            _collectFirstPara = false;
                            std::string label = _heldLabel;
                            if (label.empty()) label = t.size() <= 80 ? t : std::string();
                            else if (t.size() <= 60) label += " " + t;
                            _heldLabel.clear();
                            if (!label.empty() && _topSections > 1) tocLabel(1, label);
                        }
                    }
                }
            }
            return;
        case E_SECTION:
            if (!_notesBody && _sectionDepth <= 3) tocFinishOpen(_sectionDepth);
            if (_sectionDepth == 1) _collectFirstPara = false;
            _sectionDepth--;
            _pendingEmpty = 0;
            // An empty section: its link target stays, if a file is open to take it.
            if (!_pendingAnchor.empty() && _chapterOpen) anchorSpan();
            return;
        default:
            return;
    }
}

void Fb2Converter::text(const char* s, int len) {
    if (_skipDepth > 0 || _depth == 0) return;
    if (_descDepth > 0) {
        if (_inTitleInfo) appendCollapsed(_descText, s, len, 2000);
        return;
    }
    if (_inBinary) {
        binaryData(s, len);
        return;
    }
    if (_body == 0 || _stack[_depth - 1] == E_BINARY) return;
    _weight += (uint32_t)len;
    if (_textDepth == 0) return;   // between blocks: layout whitespace
    if (_titleDepth) appendCollapsed(_titleText, s, len, 400);
    if (_collectFirstPara && _firstParaText.size() < 200) appendCollapsed(_firstParaText, s, len, 200);
    if (_pass == 2 && !_titleDepth) {
        if (_fileLabels.size() < _file) _fileLabels.resize(_file);
        std::string& l = _fileLabels[_file - 1];
        if (l.size() < 96) appendCollapsed(l, s, len, 96);
    }
    if (_pass == 2) emitText(s, len);
}

// ------------------------------------------------------------- NCX and OPF

bool Fb2Converter::writeNcx() {
    const std::string uid = "fb2-" + std::to_string(hashId(_bookTitle.c_str()));
    std::string s =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head>"
        "<meta name=\"dtb:uid\" content=\"" + uid + "\"/></head><docTitle><text>" + xmlEscape(_bookTitle) +
        "</text></docTitle><navMap>\n";
    if (!_zip.open("OEBPS/toc.ncx")) return false;
    _zip.write(s.data(), (uint32_t)s.size());
    uint32_t order = 0;
    auto navPoint = [&](const std::string& label, uint16_t file, const std::string& anchor) {
        char src[24];
        snprintf(src, sizeof(src), "c%04u.xhtml", (unsigned)file);
        order++;
        s = "<navPoint id=\"n" + std::to_string(order) + "\" playOrder=\"" + std::to_string(order) +
            "\"><navLabel><text>" + xmlEscape(label) + "</text></navLabel><content src=\"" + src +
            (anchor.empty() ? std::string() : "#" + xmlEscape(anchor)) + "\"/>";
        _zip.write(s.data(), (uint32_t)s.size());
    };
    if (_tocCount <= 1 && _maxFile > 1) {
        // No contents of the book's own (or one entry for all of it), but several
        // files: one entry per file, named by its first words, so that the reader
        // can go from part to part.
        for (uint16_t f = 1; f <= _maxFile; f++) {
            std::string raw = f <= _fileLabels.size() ? _fileLabels[f - 1] : std::string(), l;
            for (size_t at; (at = raw.find("\xC2\xA0")) != std::string::npos;) raw.replace(at, 2, " ");   // no-break spaces
            appendCollapsed(l, raw.data(), (int)raw.size(), raw.size());
            trim(l);
            if (l.size() > 56) {   // bytes: ~28 Cyrillic letters
                size_t cut = l.rfind(' ', 56);
                if (cut == std::string::npos || cut < 24) {
                    cut = 56;
                    while (cut > 0 && ((uint8_t)l[cut] & 0xC0) == 0x80) cut--;   // not inside a UTF-8 character
                }
                l = l.substr(0, cut) + "\xE2\x80\xA6";
            }
            navPoint(std::to_string(f) + (l.empty() ? "" : ". " + l), f, std::string());
            _zip.write("</navPoint>\n", 12);
        }
    } else if (_tmpOk && _tocCount > 0 && _tmp.beginRead()) {
        int open = 0;
        for (;;) {
            uint8_t head[4];
            if (_tmp.read(head, 4) != 4) break;
            const int depth = head[0];
            const uint16_t file = (uint16_t)(head[1] | (head[2] << 8));
            std::string anchor(head[3], '\0');
            uint8_t lb[2];
            if ((head[3] && _tmp.read(&anchor[0], head[3]) != head[3]) || _tmp.read(lb, 2) != 2) break;
            const uint16_t ll = (uint16_t)(lb[0] | (lb[1] << 8));
            std::string label(ll, '\0');
            if (ll && _tmp.read(&label[0], ll) != ll) break;
            if (file == 0 || file > _maxFile) continue;
            const int d = std::min(depth, open + 1);
            while (open >= d) {
                _zip.write("</navPoint>\n", 12);
                open--;
            }
            navPoint(label, file, anchor);
            open++;
        }
        while (open > 0) {
            _zip.write("</navPoint>\n", 12);
            open--;
        }
    }
    if (order == 0) {   // no contents in the book: the whole book as one entry
        navPoint(_bookTitle.empty() ? std::string("1") : _bookTitle, 1, std::string());
        _zip.write("</navPoint>\n", 12);
    }
    _tocCount = order;
    _zip.write("</navMap></ncx>\n", 16);
    return _zip.close();
}

bool Fb2Converter::writeOpf() {
    if (!_zip.open("OEBPS/content.opf")) return false;
    auto put = [&](const std::string& s) { _zip.write(s.data(), (uint32_t)s.size()); };
    const std::string uid = "fb2-" + std::to_string(hashId(_bookTitle.c_str()));
    put("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"uid\">\n"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">\n");
    put("<dc:identifier id=\"uid\">" + uid + "</dc:identifier>\n");
    put("<dc:title>" + xmlEscape(_bookTitle.empty() ? std::string("?") : _bookTitle) + "</dc:title>\n");
    for (const std::string& a : _authors) put("<dc:creator opf:role=\"aut\">" + xmlEscape(a) + "</dc:creator>\n");
    put("<dc:language>" + xmlEscape(_lang.empty() ? std::string("en") : _lang) + "</dc:language>\n");
    if (!_annotation.empty()) put("<dc:description>" + xmlEscape(_annotation) + "</dc:description>\n");
    if (!_seriesName.empty()) {
        put("<meta name=\"calibre:series\" content=\"" + xmlEscape(_seriesName) + "\"/>\n");
        if (!_seriesNumber.empty())
            put("<meta name=\"calibre:series_index\" content=\"" + xmlEscape(_seriesNumber) + "\"/>\n");
    }
    const int cover = imageRef(_coverHref.c_str());
    char buf[160];
    if (cover >= 0 && _imgWritten[cover]) {
        snprintf(buf, sizeof(buf), "<meta name=\"cover\" content=\"img%04d\"/>\n", cover);
        put(buf);
    }
    put("</metadata>\n<manifest>\n"
        "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n"
        "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n");
    for (uint16_t f = 1; f <= _maxFile; f++) {
        snprintf(buf, sizeof(buf), "<item id=\"c%04u\" href=\"c%04u.xhtml\" media-type=\"application/xhtml+xml\"/>\n",
                 (unsigned)f, (unsigned)f);
        put(buf);
    }
    // Only the images whose data was written (an id without a <binary> is skipped).
    for (int i = 0; i < (int)_imgHash.size(); i++) {
        if (!_imgWritten[i]) continue;
        snprintf(buf, sizeof(buf), "<item id=\"img%04d\" href=\"images/i%04d\" media-type=\"%s\"%s/>\n", i, i,
                 kImageMime[_imgType[i]], i == cover ? " properties=\"cover-image\"" : "");
        put(buf);
    }
    put("</manifest>\n<spine toc=\"ncx\">\n");
    for (uint16_t f = 1; f <= _maxFile; f++) {
        snprintf(buf, sizeof(buf), "<itemref idref=\"c%04u\"/>\n", (unsigned)f);
        put(buf);
    }
    put("</spine>\n</package>\n");
    return _zip.close();
}

}  // namespace

ConvertResult fb2ToEpub(InFile& in, OutFile& out, TempFile& tmp, ProgressFn progress, void* user,
                        ConvertStats* stats) {
    ConvertStats local;
    ConvertStats& s = stats ? *stats : local;
    Fb2Converter* c = new (std::nothrow) Fb2Converter(in, out, tmp, progress, user, s);
    if (!c) return ConvertResult::NoMemory;
    const ConvertResult r = c->run();
    delete c;
    return r;
}

}  // namespace convert
