#include "mobi_to_epub.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <new>
#include <string>
#include <vector>

#include "zip_writer.h"

namespace convert {

namespace {

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

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

void appendCollapsed(std::string& out, const char* s, size_t len, size_t cap) {
    for (size_t i = 0; i < len && out.size() < cap; i++) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += c;
        }
    }
}

void trim(std::string& s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') i++;
    s.erase(0, i);
}

// Characters 0x80..0x9F of windows-1252 (the rest of the upper half is Latin-1).
const uint16_t kCp1252[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                              0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                              0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

void putUtf8(std::string& o, uint32_t cp) {
    if (cp < 0x80) {
        o += (char)cp;
    } else if (cp < 0x800) {
        o += (char)(0xC0 | (cp >> 6));
        o += (char)(0x80 | (cp & 0x3F));
    } else {
        o += (char)(0xE0 | (cp >> 12));
        o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    }
}

// ------------------------------------------------------------------ PalmDB

struct PalmDb {
    InFile* in = nullptr;
    uint64_t size = 0;
    std::vector<uint32_t> offsets;   // + end
    uint64_t pos = UINT64_MAX;

    bool open(InFile& f) {
        in = &f;
        size = f.size();
        uint8_t h[78];
        if (!f.rewind() || f.read(h, 78) != 78) return false;
        const uint16_t n = be16(h + 76);
        if (n == 0) return false;
        offsets.resize(n + 1);
        std::vector<uint8_t> list((size_t)n * 8);
        if (f.read(list.data(), (uint32_t)list.size()) != (int32_t)list.size()) return false;
        for (uint16_t i = 0; i < n; i++) offsets[i] = be32(&list[(size_t)i * 8]);
        offsets[n] = (uint32_t)size;
        for (uint16_t i = 0; i < n; i++)
            if (offsets[i] > offsets[i + 1] || offsets[i + 1] > size) return false;
        pos = 78 + list.size();
        memcpy(type, h + 60, 8);
        return true;
    }
    uint32_t count() const { return offsets.empty() ? 0 : (uint32_t)offsets.size() - 1; }
    uint32_t length(uint32_t i) const { return i < count() ? offsets[i + 1] - offsets[i] : 0; }
    // Reads record i (up to cap bytes): its length, or -1.
    int32_t read(uint32_t i, uint8_t* dst, uint32_t cap) {
        if (i >= count()) return -1;
        const uint32_t n = std::min(length(i), cap);
        if (pos != offsets[i] && !in->seek(offsets[i])) return -1;
        const int32_t r = in->read(dst, n);
        pos = r == (int32_t)n ? offsets[i] + n : UINT64_MAX;
        return r == (int32_t)n ? (int32_t)n : -1;
    }
    char type[8] = {};
};

struct MobiHeader {
    uint16_t compression = 0;
    uint32_t textLength = 0;
    uint16_t textRecords = 0;
    uint16_t encryption = 0;
    uint32_t headerLength = 0;
    uint32_t encoding = 65001;
    uint32_t version = 0;
    uint32_t firstImage = 0xFFFFFFFF;
    uint16_t extraFlags = 0;
    uint32_t kf8Boundary = 0xFFFFFFFF;   // EXTH 121
    uint32_t coverOffset = 0xFFFFFFFF;   // EXTH 201, from firstImage
    std::string title, lang, description;
    std::vector<std::string> authors;
};

bool parseRecord0(const uint8_t* r, uint32_t n, MobiHeader& h) {
    if (n < 16) return false;
    h.compression = be16(r);
    h.textLength = be32(r + 4);
    h.textRecords = be16(r + 8);
    h.encryption = be16(r + 12);
    if (n < 0x84 || memcmp(r + 16, "MOBI", 4) != 0) return true;   // plain PalmDOC
    h.headerLength = be32(r + 20);
    h.encoding = be32(r + 28);
    h.version = be32(r + 36);
    const uint32_t nameOff = be32(r + 0x54), nameLen = be32(r + 0x58);
    if (nameOff + nameLen <= n) h.title.assign((const char*)r + nameOff, nameLen);
    h.firstImage = be32(r + 0x6C);
    const uint32_t exthFlags = be32(r + 0x80);
    if (h.headerLength >= 0xE4 && 16 + 0xE4 <= n) h.extraFlags = be16(r + 0xF2);
    const uint32_t exth = 16 + h.headerLength;
    if ((exthFlags & 0x40) && exth + 12 <= n && memcmp(r + exth, "EXTH", 4) == 0) {
        const uint32_t count = be32(r + exth + 8);
        uint32_t p = exth + 12;
        for (uint32_t i = 0; i < count && p + 8 <= n; i++) {
            const uint32_t type = be32(r + p), len = be32(r + p + 4);
            if (len < 8 || p + len > n) break;
            const uint8_t* v = r + p + 8;
            const uint32_t vl = len - 8;
            std::string s((const char*)v, vl);
            switch (type) {
                case 100: if (h.authors.size() < 8) h.authors.push_back(s); break;
                case 503: h.title = s; break;
                case 524: h.lang = s; break;
                case 103: if (h.description.empty()) h.description = s; break;
                case 121: if (vl >= 4) h.kf8Boundary = be32(v); break;
                case 201: if (vl >= 4) h.coverOffset = be32(v); break;
                default: break;
            }
            p += len;
        }
    }
    return true;
}

// Bytes at the end of a text record that are not text (per the header's flags).
uint32_t trailingSize(const uint8_t* d, uint32_t n, uint16_t flags) {
    uint32_t num = 0;
    for (uint16_t f = flags >> 1; f; f >>= 1) {
        if (!(f & 1)) continue;
        // A size written backwards: 7 bits a byte, the high bit marks the first.
        uint32_t v = 0;
        const uint32_t end = n - num;
        for (uint32_t k = end >= 4 ? end - 4 : 0; k < end; k++) {
            if (d[k] & 0x80) v = 0;
            v = (v << 7) | (d[k] & 0x7F);
        }
        num += v;
        if (num > n) return n;
    }
    if ((flags & 1) && num < n) num += (d[n - num - 1] & 3) + 1;
    return std::min(num, n);
}

uint32_t palmDoc(const uint8_t* in, uint32_t n, uint8_t* out, uint32_t cap) {
    uint32_t o = 0, i = 0;
    while (i < n && o < cap) {
        const uint8_t c = in[i++];
        if (c == 0 || (c >= 9 && c <= 0x7F)) {
            out[o++] = c;
        } else if (c <= 8) {
            for (uint32_t k = 0; k < c && i < n && o < cap; k++) out[o++] = in[i++];
        } else if (c >= 0xC0) {
            out[o++] = ' ';
            if (o < cap) out[o++] = c ^ 0x80;
        } else {
            if (i >= n) break;
            const uint16_t v = (uint16_t)((c << 8) | in[i++]);
            const uint32_t dist = (v >> 3) & 0x7FF, len = (v & 7) + 3;
            if (dist == 0 || dist > o) break;
            for (uint32_t k = 0; k < len && o < cap; k++, o++) out[o] = out[o - dist];
        }
    }
    return o;
}

// ------------------------------------------------------------------ HTML

struct Tag {
    std::string name;   // lower case, no '/'
    bool end = false;
    bool selfClose = false;
    std::vector<std::pair<std::string, std::string>> attrs;
    const char* get(const char* n) const {
        for (const auto& a : attrs)
            if (a.first == n) return a.second.c_str();
        return nullptr;
    }
};

void parseTag(const std::string& raw, Tag& t) {
    t.name.clear();
    t.attrs.clear();
    t.end = t.selfClose = false;
    size_t i = 0;
    const size_t n = raw.size();
    if (i < n && raw[i] == '/') {
        t.end = true;
        i++;
    }
    while (i < n && !isspace((unsigned char)raw[i]) && raw[i] != '/' && raw[i] != '>') t.name += (char)tolower(raw[i++]);
    while (i < n) {
        while (i < n && (isspace((unsigned char)raw[i]) || raw[i] == '/')) {
            if (raw[i] == '/' && i + 1 >= n) t.selfClose = true;
            i++;
        }
        if (i >= n) break;
        std::string an, av;
        while (i < n && !isspace((unsigned char)raw[i]) && raw[i] != '=' && raw[i] != '/') an += (char)tolower(raw[i++]);
        while (i < n && isspace((unsigned char)raw[i])) i++;
        if (i < n && raw[i] == '=') {
            i++;
            while (i < n && isspace((unsigned char)raw[i])) i++;
            if (i < n && (raw[i] == '"' || raw[i] == '\'')) {
                const char q = raw[i++];
                while (i < n && raw[i] != q) av += raw[i++];
                i++;
            } else {
                while (i < n && !isspace((unsigned char)raw[i])) av += raw[i++];
                if (!av.empty() && av.back() == '/' && i >= n) {
                    av.pop_back();
                    t.selfClose = true;
                }
            }
        }
        if (!an.empty() && t.attrs.size() < 16) t.attrs.emplace_back(an, av);
    }
    if (!raw.empty() && raw.back() == '/') t.selfClose = true;
}

enum Kind : uint8_t { K_DROP, K_SKIP, K_BLOCK, K_INLINE, K_VOID };

struct ElemInfo {
    const char* in;
    const char* out;
    Kind kind;
};
const ElemInfo kElems[] = {
    {"p", "p", K_BLOCK}, {"div", "div", K_BLOCK}, {"h1", "h1", K_BLOCK}, {"h2", "h2", K_BLOCK},
    {"h3", "h3", K_BLOCK}, {"h4", "h4", K_BLOCK}, {"h5", "h5", K_BLOCK}, {"h6", "h6", K_BLOCK},
    {"blockquote", "blockquote", K_BLOCK}, {"center", "div", K_BLOCK}, {"pre", "pre", K_BLOCK},
    {"table", "table", K_BLOCK}, {"tbody", "tbody", K_BLOCK}, {"thead", "thead", K_BLOCK},
    {"tfoot", "tfoot", K_BLOCK}, {"tr", "tr", K_BLOCK}, {"td", "td", K_BLOCK}, {"th", "th", K_BLOCK},
    {"ul", "ul", K_BLOCK}, {"ol", "ol", K_BLOCK}, {"li", "li", K_BLOCK}, {"dl", "dl", K_BLOCK},
    {"dt", "dt", K_BLOCK}, {"dd", "dd", K_BLOCK},
    {"a", "a", K_INLINE}, {"b", "b", K_INLINE}, {"i", "i", K_INLINE}, {"u", "u", K_INLINE},
    {"em", "em", K_INLINE}, {"strong", "strong", K_INLINE}, {"span", "span", K_INLINE}, {"font", "span", K_INLINE},
    {"sub", "sub", K_INLINE}, {"sup", "sup", K_INLINE}, {"small", "small", K_INLINE}, {"big", "big", K_INLINE},
    {"code", "code", K_INLINE}, {"tt", "code", K_INLINE}, {"s", "del", K_INLINE}, {"strike", "del", K_INLINE},
    {"del", "del", K_INLINE}, {"ins", "ins", K_INLINE}, {"cite", "cite", K_INLINE}, {"q", "q", K_INLINE},
    {"var", "var", K_INLINE}, {"kbd", "kbd", K_INLINE}, {"abbr", "abbr", K_INLINE},
    {"br", "br", K_VOID}, {"hr", "hr", K_VOID}, {"img", "img", K_VOID},
    {"head", "", K_SKIP}, {"script", "", K_SKIP}, {"style", "", K_SKIP}, {"title", "", K_SKIP},
};

const ElemInfo* elemInfo(const std::string& n) {
    for (const auto& e : kElems)
        if (n == e.in) return &e;
    return nullptr;
}

bool isAutoCloser(const std::string& opened, const std::string& top) {
    // A new block closes an open paragraph; li/tr/td/dt/dd close their own kind.
    if (top == "p") return true;
    if (opened == "li" && top == "li") return true;
    if (opened == "tr" && (top == "tr" || top == "td" || top == "th")) return true;
    if ((opened == "td" || opened == "th") && (top == "td" || top == "th")) return true;
    if ((opened == "dt" || opened == "dd") && (top == "dt" || top == "dd")) return true;
    return false;
}

// <font size>: HTML sizes 1..7 (3 is the text's own) as em, 0 for none.
float fontEm(const Tag& t) {
    static const float em[] = {0.8f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 2.0f};
    const char* sz = t.get("size");
    if (!sz || !*sz) return 0;
    int v = atoi(sz);
    if (sz[0] == '+' || sz[0] == '-') v += 3;
    if (v < 1) v = 1;
    if (v > 7) v = 7;
    return em[v - 1];
}

std::string emStyle(float em) {
    char b[32];
    snprintf(b, sizeof(b), "font-size:%.3gem;", em);
    return b;
}

constexpr uint32_t kHardSplit = 64 * 1024;   // a file never grows past this (source bytes)
constexpr int kMaxStack = 48;

const char* const kCss =
    "body{margin:0}\n"
    "p{margin:0;text-align:justify}\n"
    "h1,h2,h3,h4,h5,h6{text-align:center;margin:1em 0 0.5em 0}\n"
    "div.center{text-align:center}\n"
    "blockquote{margin:0.5em 0 0.5em 2em}\n";

// ------------------------------------------------------------------ converter

class MobiConverter {
public:
    MobiConverter(InFile& in, OutFile& out, TempFile& tmp, ProgressFn p, void* u, ConvertStats& s)
        : _in(in), _out(out), _tmp(tmp), _progress(p), _user(u), _stats(s) {}
    ConvertResult run();

private:
    ConvertResult pass(int which);
    void feed(const uint8_t* d, uint32_t n, uint32_t base);
    void onText(const char* s, uint32_t n, uint32_t offset);
    void onTag(const std::string& raw, uint32_t offset);
    void anchorsUpTo(uint32_t offset);
    void openTag(const Tag& t, const ElemInfo& e, const std::string& extraStyle = std::string());
    void flushPending(float em = 0);
    float blockEm() const;
    void closeTo(int depth);
    void boundary(uint32_t offset);
    void emit(const char* s) { emit(s, (uint32_t)strlen(s)); }
    void emit(const char* s, uint32_t n);
    void emitText(const char* s, uint32_t n);
    void ensureChapter();
    void closeChapter();
    uint16_t fileOf(uint32_t offset) const;
    int imageRef(uint32_t recindex);
    bool writeImages();
    bool writeNcx();
    bool writeOpf();
    std::string toUtf8(const std::string& s) const;
    std::string styleFor(const Tag& t, const std::string& name) const;

    InFile& _in;
    OutFile& _out;
    TempFile& _tmp;
    ProgressFn _progress;
    void* _user;
    ConvertStats& _stats;
    PalmDb _db;
    MobiHeader _h;
    ZipWriter _zip;
    int _pass = 0;
    int _lastPermille = -1;

    // tokenizer
    enum { T_TEXT, T_TAG } _tstate = T_TEXT;
    std::string _tag;
    uint32_t _tagStart = 0;
    char _quote = 0;
    std::string _text;
    uint32_t _textStart = 0;

    // document state
    bool _inBody = false;
    int _skip = 0;               // inside head/script/style
    std::string _stack[kMaxStack];
    float _em[kMaxStack];          // a block's font size (em) when its first <font> was moved onto it
    // A text block's start tag waits for its first content: when that is a
    // <font size>, the size goes on the block. The engine spaces a paragraph's
    // lines by the paragraph's size, so a big <font> inside a normal <p> (how
    // MOBI headings are made) would overprint its own lines.
    Tag _pTag[kMaxStack];
    uint32_t _pBytes = 0;          // text written into the open p (pass 2)          // the start tag of each open p (pass 2): re-opened after a <br>
    bool _pending = false;
    Tag _pendingTag;
    const ElemInfo* _pendingInfo = nullptr;
    int _depth = 0;
    uint32_t _fileStart = 0;     // source offset where the current file began
    bool _fileHasContent = false;
    uint16_t _file = 1;
    uint16_t _maxFile = 0;
    bool _chapterOpen = false;
    std::vector<uint32_t> _bounds;     // pass 1: source offsets where files 2.. begin
    size_t _nextBound = 0;
    std::vector<uint32_t> _targets;    // filepos targets, sorted
    size_t _nextTarget = 0;
    uint32_t _tocStart = UINT32_MAX;   // guide: the contents page
    bool _inToc = false;
    bool _tocDone = false;
    int _tocLink = -1;                 // collecting a contents label for this target
    std::string _tocLabel;
    uint32_t _tocCount = 0;
    std::vector<std::string> _fileLabels;
    std::vector<uint32_t> _imgRec;     // recindex of each image, in order of first use
    std::vector<bool> _imgWritten;
    int _cover = -1;
    char _buf[2048];
    uint32_t _bufLen = 0;
    uint8_t _rec[8192];
    uint8_t _plain[8192];
};

ConvertResult MobiConverter::run() {
    if (!_db.open(_in)) return ConvertResult::NotThisFormat;
    if (memcmp(_db.type, "BOOKMOBI", 8) != 0 && memcmp(_db.type, "TEXtREAd", 8) != 0) return ConvertResult::NotThisFormat;
    const int32_t n0 = _db.read(0, _rec, sizeof(_rec));
    if (n0 < 16 || !parseRecord0(_rec, (uint32_t)n0, _h)) return ConvertResult::ParseError;
    if (_h.encryption != 0) {
        _stats.error = "DRM-protected book";
        return ConvertResult::Unsupported;
    }
    if (_h.version >= 8 && _h.kf8Boundary == 0xFFFFFFFF) {
        _stats.error = "KF8 (AZW3) book";
        return ConvertResult::Unsupported;
    }
    if (_h.compression != 1 && _h.compression != 2) {
        _stats.error = _h.compression == 17480 ? "HUFF/CDIC compression" : "unknown compression";
        return ConvertResult::Unsupported;
    }
    if (_h.textRecords == 0 || _h.textRecords >= _db.count()) return ConvertResult::ParseError;
    if (_h.encoding != 65001) {   // the title in the header is in the book's code page too
        _h.title = toUtf8(_h.title);
        for (auto& a : _h.authors) a = toUtf8(a);
        _h.description = toUtf8(_h.description);
    }

    ConvertResult r = pass(1);
    if (r != ConvertResult::Ok) return r;
    std::sort(_targets.begin(), _targets.end());
    _targets.erase(std::unique(_targets.begin(), _targets.end()), _targets.end());

    _zip.begin(&_out);
    _zip.add("mimetype", "application/epub+zip");
    _zip.add("META-INF/container.xml",
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
             "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">"
             "<rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
             "</rootfiles></container>\n");
    _zip.add("OEBPS/style.css", kCss);
    if (_h.coverOffset != 0xFFFFFFFF) _cover = imageRef(_h.coverOffset + 1);
    r = pass(2);
    if (r != ConvertResult::Ok) return r;
    closeChapter();
    if (_maxFile == 0) {
        _fileHasContent = true;
        ensureChapter();
        emit("<p>&#160;</p>");
        closeChapter();
    }
    if (!writeImages() || !writeNcx() || !writeOpf() || !_zip.finish()) {
        if (_stats.error.empty()) _stats.error = "writing the EPUB failed";
        return ConvertResult::IoError;
    }
    _stats.chapters = _maxFile;
    _stats.ids = (uint32_t)_targets.size();
    _stats.title = _h.title;
    _stats.lang = _h.lang;
    for (size_t i = 0; i < _h.authors.size(); i++) _stats.author += (i ? ", " : "") + _h.authors[i];
    if (_progress) _progress(_user, 1000);
    return ConvertResult::Ok;
}

ConvertResult MobiConverter::pass(int which) {
    _pass = which;
    _tstate = T_TEXT;
    _tag.clear();
    _text.clear();
    _quote = 0;
    _inBody = false;
    _skip = 0;
    _depth = 0;
    _fileStart = 0;
    _fileHasContent = false;
    _file = 1;
    _maxFile = 0;
    _chapterOpen = false;
    _nextBound = 0;
    _nextTarget = 0;
    _inToc = false;
    _tocDone = false;
    _tocLink = -1;
    _bufLen = 0;
    if (which == 1) _tmp.beginWrite();
    uint32_t produced = 0;
    for (uint32_t i = 1; i <= _h.textRecords && produced < _h.textLength; i++) {
        const int32_t n = _db.read(i, _rec, sizeof(_rec));
        if (n < 0) {
            _stats.error = "cannot read the book";
            return ConvertResult::IoError;
        }
        const uint32_t body = (uint32_t)n - trailingSize(_rec, (uint32_t)n, _h.extraFlags);
        uint32_t m;
        if (_h.compression == 2) {
            m = palmDoc(_rec, body, _plain, sizeof(_plain));
        } else {
            m = std::min<uint32_t>(body, sizeof(_plain));
            memcpy(_plain, _rec, m);
        }
        m = std::min(m, _h.textLength - produced);
        feed(_plain, m, produced);
        produced += m;
        if (_progress) {
            const int pm = (which == 1 ? 0 : 300) + (int)((which == 1 ? 300 : 500) * (uint64_t)i / _h.textRecords);
            if (pm / 10 != _lastPermille / 10) {
                _lastPermille = pm;
                _progress(_user, pm);
            }
        }
        if (_zip.failed()) return ConvertResult::IoError;
    }
    // what is left in the tokenizer
    if (!_text.empty()) onText(_text.data(), (uint32_t)_text.size(), _textStart);
    _text.clear();
    closeTo(0);
    return ConvertResult::Ok;
}

void MobiConverter::feed(const uint8_t* d, uint32_t n, uint32_t base) {
    for (uint32_t i = 0; i < n; i++) {
        const char c = (char)d[i];
        if (_tstate == T_TEXT) {
            if (c == '<') {
                if (!_text.empty()) onText(_text.data(), (uint32_t)_text.size(), _textStart);
                _text.clear();
                _tstate = T_TAG;
                _tag.clear();
                _tagStart = base + i;
                _quote = 0;
            } else {
                if (_text.empty()) _textStart = base + i;
                _text += c;
                if (_text.size() >= 1024 && c == ' ') {   // long runs go out in pieces
                    onText(_text.data(), (uint32_t)_text.size(), _textStart);
                    _text.clear();
                }
            }
            continue;
        }
        // in a tag (or comment)
        const bool comment = _tag.size() >= 3 && _tag.compare(0, 3, "!--") == 0;
        if (comment) {
            _tag += c;
            if (c == '>' && _tag.size() >= 5 && _tag.compare(_tag.size() - 3, 3, "-->") == 0) _tstate = T_TEXT;
            if (_tag.size() > 4096) _tag.erase(3, _tag.size() - 6);
            continue;
        }
        if (_quote) {
            if (c == _quote) _quote = 0;
            if (_tag.size() < 4096) _tag += c;
            continue;
        }
        if (c == '"' || c == '\'') {
            // quotes only around attribute values (after '=')
            size_t k = _tag.size();
            while (k > 0 && isspace((unsigned char)_tag[k - 1])) k--;
            if (k > 0 && _tag[k - 1] == '=') _quote = c;
        }
        if (c == '>') {
            _tstate = T_TEXT;
            onTag(_tag, _tagStart);
            continue;
        }
        if (_tag.size() < 4096) _tag += c;
    }
}

// ---------------------------------------------------------------- output

std::string MobiConverter::toUtf8(const std::string& s) const {
    if (_h.encoding == 65001) return s;
    std::string o;
    for (unsigned char c : s) {
        if (c < 0x80) o += (char)c;
        else putUtf8(o, c < 0xA0 ? kCp1252[c - 0x80] : c);
    }
    return o;
}

void MobiConverter::emit(const char* s, uint32_t n) {
    if (_pass != 2) return;
    ensureChapter();
    while (n) {
        const uint32_t k = std::min<uint32_t>(n, sizeof(_buf) - _bufLen);
        memcpy(_buf + _bufLen, s, k);
        _bufLen += k;
        s += k;
        n -= k;
        if (_bufLen == sizeof(_buf)) {
            _zip.write(_buf, _bufLen);
            _bufLen = 0;
        }
    }
}

// Text as it stands in the HTML: entities stay (the engine reads them), a bare
// '&' or '<' is escaped; non-UTF-8 books are converted.
void MobiConverter::emitText(const char* s, uint32_t n) {
    if (_pass != 2) return;
    std::string o;
    o.reserve(n + 16);
    for (uint32_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '&') {
            uint32_t k = i + 1;
            while (k < n && k - i < 12 && (isalnum((unsigned char)s[k]) || s[k] == '#')) k++;
            if (k < n && s[k] == ';' && k > i + 1) o += '&';
            else o += "&amp;";
        } else if (c == '<') {
            o += "&lt;";
        } else if (c == '>') {
            o += "&gt;";
        } else if (c >= 0x80 && _h.encoding != 65001) {
            putUtf8(o, c < 0xA0 ? kCp1252[c - 0x80] : c);
        } else {
            o += (char)c;
        }
    }
    emit(o.data(), (uint32_t)o.size());
}

void MobiConverter::ensureChapter() {
    if (_chapterOpen || _pass != 2) return;
    char name[32];
    snprintf(name, sizeof(name), "OEBPS/c%04u.xhtml", (unsigned)_file);
    _zip.open(name);
    _chapterOpen = true;
    const std::string head =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE html>\n"
        "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"" + xmlEscape(_h.lang.empty() ? "en" : _h.lang) +
        "\"><head><title>" + xmlEscape(_h.title) +
        "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head><body>\n";
    emit(head.data(), (uint32_t)head.size());
}

void MobiConverter::closeChapter() {
    if (_chapterOpen) {
        emit("\n</body></html>\n");
        if (_bufLen) _zip.write(_buf, _bufLen);
        _bufLen = 0;
        _zip.close();
        _chapterOpen = false;
    }
    if (_fileHasContent && _file > _maxFile) _maxFile = _file;
}

// A new file from `offset` on (pass 1 decides, pass 2 follows the list).
void MobiConverter::boundary(uint32_t offset) {
    if (!_fileHasContent) return;
    closeTo(0);
    closeChapter();
    _file++;
    _fileHasContent = false;
    _fileStart = offset;
    if (_pass == 1) _bounds.push_back(offset);
}

uint16_t MobiConverter::fileOf(uint32_t offset) const {
    return (uint16_t)(1 + (std::upper_bound(_bounds.begin(), _bounds.end(), offset) - _bounds.begin()));
}

int MobiConverter::imageRef(uint32_t recindex) {
    if (recindex == 0 || _h.firstImage == 0xFFFFFFFF) return -1;
    for (size_t i = 0; i < _imgRec.size(); i++)
        if (_imgRec[i] == recindex) return (int)i;
    if (_imgRec.size() >= 4096) return -1;
    _imgRec.push_back(recindex);
    _imgWritten.push_back(false);
    return (int)_imgRec.size() - 1;
}

void MobiConverter::anchorsUpTo(uint32_t offset) {
    while (_nextTarget < _targets.size() && _targets[_nextTarget] <= offset) {
        if (_pass == 2) {
            char a[40];
            snprintf(a, sizeof(a), "<a id=\"fp%u\"></a>", (unsigned)_targets[_nextTarget]);
            emit(a);
        }
        _nextTarget++;
    }
}

void MobiConverter::closeTo(int depth) {
    if (_pending && _depth > depth) flushPending();
    while (_depth > depth) {
        _depth--;
        const ElemInfo* e = elemInfo(_stack[_depth]);
        if (_pass == 2 && e) {
            emit("</");
            emit(e->out);
            emit(e->kind == K_BLOCK ? ">\n" : ">");
        }
    }
}

std::string MobiConverter::styleFor(const Tag& t, const std::string& name) const {
    std::string st;
    auto len = [](const char* v) {
        // "1em", "0pt", "-14pt", "10%", "3": a CSS length (a bare number is pixels)
        std::string s = v;
        if (s.empty()) return s;
        bool num = true;
        for (char c : s)
            if (!(isdigit((unsigned char)c) || c == '.' || c == '-')) num = false;
        return num ? s + "px" : s;
    };
    if (const char* a = t.get("align")) {
        std::string v = a;
        for (char& c : v) c = (char)tolower(c);
        if (v == "center" || v == "left" || v == "right" || v == "justify") st += "text-align:" + v + ";";
    }
    if (name == "p" || name == "div" || name == "blockquote") {
        // (zero is the default; negative, a hanging indent of a contents page, is dropped)
        if (const char* w = t.get("width")) {
            const std::string l = len(w);
            if (atof(l.c_str()) > 0) st += "text-indent:" + l + ";";
        }
        if (const char* h = t.get("height")) {
            const std::string l = len(h);
            if (atof(l.c_str()) > 0) st += "margin-top:" + l + ";";
        }
    }
    return st;
}

void MobiConverter::openTag(const Tag& t, const ElemInfo& e, const std::string& extraStyle) {
    if (_pass != 2) return;
    emit("<");
    emit(e.out);
    if (t.name == "center") emit(" class=\"center\"");
    const std::string st = styleFor(t, t.name) + extraStyle;
    if (!st.empty()) {
        emit(" style=\"");
        emit(st.c_str());
        emit("\"");
    }
    if (const char* id = t.get("id")) {
        emit(" id=\"");
        const std::string v = xmlEscape(toUtf8(id));
        emit(v.data(), (uint32_t)v.size());
        emit("\"");
    }
    for (const char* a : {"colspan", "rowspan"}) {
        if (const char* v = t.get(a)) {
            char b[48];
            snprintf(b, sizeof(b), " %s=\"%d\"", a, atoi(v));
            emit(b);
        }
    }
    if (t.name == "a") {
        std::string href;
        if (const char* fp = t.get("filepos")) {
            const uint32_t target = (uint32_t)strtoul(fp, nullptr, 10);
            const uint16_t f = fileOf(target);
            char b[48];
            if (f == _file) snprintf(b, sizeof(b), "#fp%u", (unsigned)target);
            else snprintf(b, sizeof(b), "c%04u.xhtml#fp%u", (unsigned)f, (unsigned)target);
            href = b;
        } else if (const char* h = t.get("href")) {
            href = toUtf8(h);
        }
        if (!href.empty()) {
            emit(" href=\"");
            const std::string v = xmlEscape(href);
            emit(v.data(), (uint32_t)v.size());
            emit("\"");
        }
    }
    if (t.name == "img") {
        const char* ri = t.get("recindex");
        const int idx = ri ? imageRef((uint32_t)strtoul(ri, nullptr, 10)) : -1;
        char b[64];
        snprintf(b, sizeof(b), " src=\"images/i%04d\" alt=\"\"", idx < 0 ? 0 : idx);
        emit(b);
        for (const char* a : {"width", "height"}) {
            if (const char* v = t.get(a)) {
                snprintf(b, sizeof(b), " %s=\"%d\"", a, atoi(v));
                emit(b);
            }
        }
    }
    emit(e.kind == K_VOID ? "/>" : ">");
}

void MobiConverter::onTag(const std::string& raw, uint32_t offset) {
    if (raw.empty() || raw[0] == '!' || raw[0] == '?') return;   // comments, doctype
    Tag t;
    parseTag(raw, t);
    if (t.name.empty()) return;

    // The guide (in the head) names the contents page.
    if (t.name == "reference" && !t.end) {
        const char* type = t.get("type");
        const char* fp = t.get("filepos");
        if (type && fp && strcasecmp(type, "toc") == 0) _tocStart = (uint32_t)strtoul(fp, nullptr, 10);
        return;
    }
    if (t.name == "body") {
        _inBody = !t.end;
        return;
    }
    const ElemInfo* e = elemInfo(t.name);
    if (e && e->kind == K_SKIP) {
        if (t.end) {
            if (_skip > 0) _skip--;
        } else if (!t.selfClose) {
            _skip++;
        }
        return;
    }
    if (_skip > 0 || !_inBody) return;

    // Pass 2 starts its files where pass 1 did.
    if (_pass == 2 && _nextBound < _bounds.size() && offset >= _bounds[_nextBound]) {
        _nextBound++;
        boundary(offset);
    }
    if (t.name == "mbp:pagebreak") {
        if (_inToc) {
            _inToc = false;
            _tocDone = true;
        }
        if (_pass == 1) boundary(offset);
        return;
    }
    if (_pass == 1 && !_tocDone && !_inToc && _tocStart != UINT32_MAX && offset >= _tocStart) _inToc = true;
    anchorsUpTo(offset);

    // Contents: each link on the contents page, labelled by its text.
    if (_pass == 1 && _inToc && t.name == "a") {
        if (!t.end) {
            if (const char* fp = t.get("filepos")) {
                _tocLink = (int)strtoul(fp, nullptr, 10);
                _tocLabel.clear();
            }
        } else if (_tocLink >= 0) {
            std::string l = toUtf8(_tocLabel);
            trim(l);
            if (!l.empty()) {
                const uint32_t target = (uint32_t)_tocLink;
                const uint16_t ll = (uint16_t)std::min<size_t>(l.size(), 600);
                uint8_t h[6] = {(uint8_t)(target >> 24), (uint8_t)(target >> 16), (uint8_t)(target >> 8),
                                (uint8_t)target, (uint8_t)(ll >> 8), (uint8_t)ll};
                _tmp.write(h, 6);
                _tmp.write(l.data(), ll);
                _tocCount++;
            }
            _tocLink = -1;
        }
    }
    if (_pass == 1 && t.name == "a" && !t.end) {
        if (const char* fp = t.get("filepos")) _targets.push_back((uint32_t)strtoul(fp, nullptr, 10));
    }

    if (!e) return;   // unknown tags (mbp:*, o:p, ...) go, their text stays
    if (_pending) {
        if (!t.end && t.name == "font" && fontEm(t) > 0) {
            // the block's first content: its size becomes the block's
            const float em = fontEm(t);
            flushPending(em);
            if (_depth < kMaxStack) {
                _em[_depth] = 0;
                _stack[_depth++] = t.name;
            }
            if (_pass == 2) emit("<span>");
            return;
        }
        flushPending();
    }
    if (t.end) {
        if (e->kind == K_VOID) return;   // </br>
        for (int d = _depth - 1; d >= 0; d--) {
            if (_stack[d] == t.name) {
                closeTo(d);
                break;
            }
        }
        return;
    }
    // A block starts: past the size limit, at the top level, a new file.
    if (e->kind == K_BLOCK || e->kind == K_VOID) {
        if (_pass == 1 && _depth == 0 && offset - _fileStart > kHardSplit) boundary(offset);
        // A block closes the inline elements and paragraph it was left inside.
        while (_depth > 0 && e->kind == K_BLOCK) {
            const ElemInfo* top = elemInfo(_stack[_depth - 1]);
            if ((top && top->kind == K_INLINE) || isAutoCloser(t.name, _stack[_depth - 1])) closeTo(_depth - 1);
            else break;
        }
    }
    _fileHasContent = true;
    if (_pass == 2 && e->kind == K_BLOCK && !t.selfClose && t.name != "table" && t.name != "tr" && t.name != "ul" &&
        t.name != "ol" && t.name != "dl" && t.name != "tbody" && t.name != "thead" && t.name != "tfoot") {
        // held until its first content (see _pending)
        _pending = true;
        _pendingTag = t;
        _pendingInfo = e;
        if (_depth < kMaxStack) {
            _em[_depth] = 0;
            _stack[_depth++] = t.name;
        }
        return;
    }
    // Many MOBI books (royallib, ...) put a whole chapter in one <p>, with <br>
    // between its paragraphs: tens of KB that the engine would have to cut
    // mid-word. A <br> straight inside a p (no inline element open, no size
    // moved onto it) closes it and starts the next one, without the space above.
    if (t.name == "br" && _pass == 2 && _depth > 0 && _stack[_depth - 1] == "p" && _em[_depth - 1] == 0) {
        Tag next = _pTag[_depth - 1];
        for (auto it = next.attrs.begin(); it != next.attrs.end();)
            it = (it->first == "height" || it->first == "id") ? next.attrs.erase(it) : it + 1;
        emit("</p>\n");
        openTag(next, *elemInfo("p"));
        _pBytes = 0;
        return;
    }
    std::string extra;
    if (t.name == "font") {
        const float em = fontEm(t), base = blockEm();
        if (em > 0 && (em / base > 1.01f || em / base < 0.99f)) extra = emStyle(em / base);
    }
    openTag(t, *e, extra);
    if (e->kind == K_VOID || t.selfClose) {
        if (e->kind != K_VOID && _pass == 2) {   // <p/>: closed at once
            emit("</");
            emit(e->out);
            emit(">");
        }
        return;
    }
    if (_depth < kMaxStack) {
        _em[_depth] = 0;
        _stack[_depth++] = t.name;
    }
}

// The font size of the innermost block that took one, 1 if none did.
float MobiConverter::blockEm() const {
    for (int d = _depth - 1; d >= 0; d--)
        if (_em[d] > 0) return _em[d];
    return 1.0f;
}

void MobiConverter::flushPending(float em) {
    if (!_pending) return;
    _pending = false;
    // the pending block is the innermost open element
    int d = _depth - 1;
    if (d >= 0) _em[d] = em > 0 ? em * (d > 0 ? 1.0f : 1.0f) : 0;
    std::string extra;
    if (em > 0) {
        float base = 1.0f;
        for (int k = d - 1; k >= 0; k--)
            if (_em[k] > 0) { base = _em[k]; break; }
        if (em / base > 1.01f || em / base < 0.99f) extra = emStyle(em / base);
        if (d >= 0) _em[d] = em;
    }
    if (d >= 0 && _pendingTag.name == "p") _pTag[d] = _pendingTag;
    _pBytes = 0;
    openTag(_pendingTag, *_pendingInfo, extra);
}

void MobiConverter::onText(const char* s, uint32_t n, uint32_t offset) {
    if (_skip > 0 || !_inBody) return;
    if (_pass == 2 && _nextBound < _bounds.size() && offset >= _bounds[_nextBound] && _depth == 0) {
        _nextBound++;
        boundary(offset);
    }
    anchorsUpTo(offset);
    bool blank = true;
    for (uint32_t i = 0; i < n && blank; i++)
        if (!isspace((unsigned char)s[i])) blank = false;
    if (blank && _depth == 0) return;
    if (_pending) {
        if (blank) return;   // spacing before the first content
        flushPending();
    }
    if (!blank) _fileHasContent = true;
    if (_tocLink >= 0 && _tocLabel.size() < 400) appendCollapsed(_tocLabel, s, n, 400);
    if (_pass == 2 && !blank) {
        if (_fileLabels.size() < _file) _fileLabels.resize(_file);
        std::string& l = _fileLabels[_file - 1];
        if (l.size() < 96) {
            if (!l.empty() && l.back() != ' ') l += ' ';
            appendCollapsed(l, s, n, 96);
        }
    }
    // The engine holds ~3 KB of a paragraph and cuts a longer one wherever it
    // is full, mid-word or mid-character: a long p goes on in a new one at a
    // space (no indent: it reads as one paragraph).
    constexpr uint32_t kParLimit = 2400;
    while (_pass == 2 && _depth > 0 && _stack[_depth - 1] == "p" && _em[_depth - 1] == 0 && _pBytes + n > kParLimit) {
        const uint32_t room = _pBytes < kParLimit ? kParLimit - _pBytes : 0;
        uint32_t cut = room < n ? room : n;
        while (cut > 0 && s[cut - 1] != ' ') cut--;
        if (cut == 0) {   // no space within the room: the first one after it
            cut = room;
            while (cut < n && s[cut] != ' ') cut++;
            if (cut >= n) break;
            cut++;
        }
        emitText(s, cut);
        Tag next = _pTag[_depth - 1];
        for (auto it = next.attrs.begin(); it != next.attrs.end();)
            it = (it->first == "height" || it->first == "id" || it->first == "width") ? next.attrs.erase(it) : it + 1;
        emit("</p>\n");
        openTag(next, *elemInfo("p"), "text-indent:0;");
        _pBytes = 0;
        s += cut;
        n -= cut;
    }
    _pBytes += n;
    emitText(s, n);
}

// ------------------------------------------------------------ images, NCX, OPF

const char* sniffMime(const uint8_t* d, uint32_t n) {
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return "image/jpeg";
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return "image/png";
    if (n >= 6 && memcmp(d, "GIF8", 4) == 0) return "image/gif";
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return "image/bmp";
    return nullptr;
}

bool MobiConverter::writeImages() {
    for (size_t i = 0; i < _imgRec.size(); i++) {
        const uint32_t rec = _h.firstImage + _imgRec[i] - 1;
        if (rec >= _db.count()) continue;
        const uint32_t len = _db.length(rec);
        if (len < 8) continue;
        // the first bytes say what it is (records past the images are not)
        uint8_t head[8];
        if (_db.read(rec, head, 8) != 8 || !sniffMime(head, 8)) continue;
        char nm[32];
        snprintf(nm, sizeof(nm), "OEBPS/images/i%04u", (unsigned)i);
        if (!_zip.open(nm)) return false;
        if (!_in.seek(_db.offsets[rec])) return false;
        _db.pos = UINT64_MAX;
        for (uint32_t done = 0; done < len;) {
            const uint32_t k = std::min<uint32_t>(len - done, sizeof(_rec));
            if (_in.read(_rec, k) != (int32_t)k) return false;
            _zip.write(_rec, k);
            done += k;
        }
        _zip.close();
        _imgWritten[i] = true;
        _stats.images++;
        if (_progress) _progress(_user, 800 + (int)(190 * (i + 1) / _imgRec.size()));
    }
    return !_zip.failed();
}

bool MobiConverter::writeNcx() {
    const std::string uid = "mobi-" + std::to_string(_h.textLength) + "-" + std::to_string(_db.count());
    std::string s = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head>"
                    "<meta name=\"dtb:uid\" content=\"" + uid + "\"/></head><docTitle><text>" + xmlEscape(_h.title) +
                    "</text></docTitle><navMap>\n";
    if (!_zip.open("OEBPS/toc.ncx")) return false;
    _zip.write(s.data(), (uint32_t)s.size());
    uint32_t order = 0;
    auto nav = [&](const std::string& label, uint16_t file, const std::string& frag) {
        char src[24];
        snprintf(src, sizeof(src), "c%04u.xhtml", (unsigned)file);
        order++;
        s = "<navPoint id=\"n" + std::to_string(order) + "\" playOrder=\"" + std::to_string(order) +
            "\"><navLabel><text>" + xmlEscape(label) + "</text></navLabel><content src=\"" + src +
            (frag.empty() ? std::string() : "#" + frag) + "\"/></navPoint>\n";
        _zip.write(s.data(), (uint32_t)s.size());
    };
    if (_tocCount > 1 && _tmp.beginRead()) {
        for (;;) {
            uint8_t h[6];
            if (_tmp.read(h, 6) != 6) break;
            const uint32_t target = be32(h);
            const uint16_t ll = be16(h + 4);
            std::string label(ll, '\0');
            if (ll && _tmp.read(&label[0], ll) != ll) break;
            const uint16_t f = fileOf(target);
            if (f > _maxFile) continue;
            nav(label, f, "fp" + std::to_string(target));
        }
    } else if (_maxFile > 1) {
        // No contents of its own: one entry per file, by its first words.
        for (uint16_t f = 1; f <= _maxFile; f++) {
            std::string l = f <= _fileLabels.size() ? _fileLabels[f - 1] : std::string();
            for (size_t at; (at = l.find("\xC2\xA0")) != std::string::npos;) l.replace(at, 2, " ");
            std::string c;
            appendCollapsed(c, l.data(), l.size(), l.size());
            trim(c);
            if (c.size() > 56) {
                size_t cut = c.rfind(' ', 56);
                if (cut == std::string::npos || cut < 24) {
                    cut = 56;
                    while (cut > 0 && ((uint8_t)c[cut] & 0xC0) == 0x80) cut--;
                }
                c = c.substr(0, cut) + "\xE2\x80\xA6";
            }
            nav(std::to_string(f) + (c.empty() ? "" : ". " + c), f, std::string());
        }
    }
    if (order == 0) nav(_h.title.empty() ? std::string("1") : _h.title, 1, std::string());
    _stats.tocEntries = order;
    _zip.write("</navMap></ncx>\n", 16);
    return _zip.close();
}

bool MobiConverter::writeOpf() {
    if (!_zip.open("OEBPS/content.opf")) return false;
    auto put = [&](const std::string& s) { _zip.write(s.data(), (uint32_t)s.size()); };
    put("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"uid\">\n"
        "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">\n");
    put("<dc:identifier id=\"uid\">mobi-" + std::to_string(_h.textLength) + "</dc:identifier>\n");
    put("<dc:title>" + xmlEscape(_h.title.empty() ? std::string("?") : _h.title) + "</dc:title>\n");
    for (const auto& a : _h.authors) put("<dc:creator opf:role=\"aut\">" + xmlEscape(a) + "</dc:creator>\n");
    put("<dc:language>" + xmlEscape(_h.lang.empty() ? std::string("en") : _h.lang) + "</dc:language>\n");
    if (!_h.description.empty()) put("<dc:description>" + xmlEscape(_h.description) + "</dc:description>\n");
    char buf[160];
    if (_cover >= 0 && _imgWritten[_cover]) {
        snprintf(buf, sizeof(buf), "<meta name=\"cover\" content=\"img%04d\"/>\n", _cover);
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
    for (size_t i = 0; i < _imgRec.size(); i++) {
        if (!_imgWritten[i]) continue;
        uint8_t head[8];
        const char* mime = _db.read(_h.firstImage + _imgRec[i] - 1, head, 8) == 8 ? sniffMime(head, 8) : nullptr;
        snprintf(buf, sizeof(buf), "<item id=\"img%04u\" href=\"images/i%04u\" media-type=\"%s\"%s/>\n", (unsigned)i,
                 (unsigned)i, mime ? mime : "image/jpeg", (int)i == _cover ? " properties=\"cover-image\"" : "");
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

MobiInfo probeMobi(InFile& in) {
    MobiInfo info;
    PalmDb db;
    if (!db.open(in)) return info;
    if (memcmp(db.type, "BOOKMOBI", 8) != 0 && memcmp(db.type, "TEXtREAd", 8) != 0) return info;
    info.isMobi = true;
    std::vector<uint8_t> r(std::min<uint32_t>(db.length(0), 16384));
    const int32_t n = db.read(0, r.data(), (uint32_t)r.size());
    MobiHeader h;
    if (n < 16 || !parseRecord0(r.data(), (uint32_t)n, h)) return info;
    info.compression = h.compression;
    info.drm = h.encryption != 0;
    info.hasKf8 = h.version >= 8 || h.kf8Boundary != 0xFFFFFFFF;
    info.kf8Only = h.version >= 8 && h.kf8Boundary == 0xFFFFFFFF;
    return info;
}

ConvertResult mobiToEpub(InFile& in, OutFile& out, TempFile& tmp, ProgressFn progress, void* user,
                         ConvertStats* stats) {
    ConvertStats local;
    ConvertStats& s = stats ? *stats : local;
    MobiConverter* c = new (std::nothrow) MobiConverter(in, out, tmp, progress, user, s);
    if (!c) return ConvertResult::NoMemory;
    const ConvertResult r = c->run();
    delete c;
    return r;
}

}  // namespace convert
