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
    uint16_t recordSize = 4096;
    // KF8 (version 8): record numbers of its tables
    uint32_t fdst = 0xFFFFFFFF, ncx = 0xFFFFFFFF, frag = 0xFFFFFFFF, skel = 0xFFFFFFFF;
    std::string title, lang, description;
    std::vector<std::string> authors;
};

bool parseRecord0(const uint8_t* r, uint32_t n, MobiHeader& h) {
    if (n < 16) return false;
    h.compression = be16(r);
    h.textLength = be32(r + 4);
    h.textRecords = be16(r + 8);
    h.recordSize = be16(r + 10);
    h.encryption = be16(r + 12);
    if (n < 0x84 || memcmp(r + 16, "MOBI", 4) != 0) return true;   // plain PalmDOC
    h.headerLength = be32(r + 20);
    h.encoding = be32(r + 28);
    h.version = be32(r + 36);
    const uint32_t nameOff = be32(r + 0x54), nameLen = be32(r + 0x58);
    if (nameOff + nameLen <= n) h.title.assign((const char*)r + nameOff, nameLen);
    h.firstImage = be32(r + 0x6C);
    if (h.version >= 8 && n >= 0x108) {
        h.fdst = be32(r + 0xC0);
        h.ncx = be32(r + 0xF4);
        h.frag = be32(r + 0xF8);
        h.skel = be32(r + 0xFC);
    }
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

// ------------------------------------------------------------------ KF8 indexes

// A forward variable-width integer: 7 bits a byte, the high bit marks the last.
uint32_t decint(const uint8_t* d, uint32_t n, uint32_t& p) {
    uint32_t v = 0;
    for (int k = 0; k < 4 && p < n; k++) {
        const uint8_t b = d[p++];
        v = (v << 7) | (b & 0x7F);
        if (b & 0x80) break;
    }
    return v;
}

struct IndexEntry {
    std::string ident;
    std::vector<std::pair<uint8_t, std::vector<uint32_t>>> tags;
    uint32_t get(uint8_t tag, size_t i, uint32_t dflt = 0xFFFFFFFF) const {
        for (const auto& t : tags)
            if (t.first == tag && i < t.second.size()) return t.second[i];
        return dflt;
    }
};

struct IndexTable {
    std::vector<IndexEntry> entries;
    std::vector<std::vector<uint8_t>> cncx;
    // A string of the CNCX records (offset: record << 16 | position).
    std::string text(uint32_t off) const {
        const uint32_t r = off >> 16, p0 = off & 0xFFFF;
        if (r >= cncx.size() || p0 >= cncx[r].size()) return std::string();
        uint32_t p = p0;
        const uint32_t len = decint(cncx[r].data(), (uint32_t)cncx[r].size(), p);
        if (p + len > cncx[r].size()) return std::string();
        return std::string((const char*)cncx[r].data() + p, len);
    }
};

bool readRecordVec(PalmDb& db, uint32_t i, std::vector<uint8_t>& out) {
    out.resize(db.length(i));
    return !out.empty() && db.read(i, out.data(), (uint32_t)out.size()) == (int32_t)out.size();
}

// An INDX table: its header record, the index records after it, then CNCX.
bool readIndex(PalmDb& db, uint32_t idx, IndexTable& t, uint32_t maxEntries = 20000) {
    std::vector<uint8_t> h;
    if (idx >= db.count() || !readRecordVec(db, idx, h) || h.size() < 192 || memcmp(h.data(), "INDX", 4) != 0)
        return false;
    const uint32_t count = be32(&h[24]), ncncx = be32(&h[52]);
    uint32_t tagx = be32(&h[180]);
    if (tagx == 0 || tagx + 12 > h.size()) tagx = be32(&h[4]);
    if (tagx + 12 > h.size() || memcmp(&h[tagx], "TAGX", 4) != 0) return false;
    const uint32_t first = be32(&h[tagx + 4]), cbc = be32(&h[tagx + 8]);
    struct TagX { uint8_t tag, values, mask, eof; };
    std::vector<TagX> tags;
    for (uint32_t i = tagx + 12; i + 4 <= tagx + first && i + 4 <= h.size(); i += 4)
        tags.push_back({h[i], h[i + 1], h[i + 2], h[i + 3]});
    for (uint32_t c = 0; c < ncncx && idx + count + 1 + c < db.count(); c++) {
        t.cncx.emplace_back();
        readRecordVec(db, idx + count + 1 + c, t.cncx.back());
    }
    std::vector<uint8_t> d;
    for (uint32_t r = idx + 1; r <= idx + count && r < db.count(); r++) {
        if (!readRecordVec(db, r, d) || d.size() < 28 || memcmp(d.data(), "INDX", 4) != 0) return false;
        const uint32_t idxt = be32(&d[20]), ec = be32(&d[24]);
        if (idxt + 4 + 2 * ec > d.size()) return false;
        for (uint32_t j = 0; j < ec && t.entries.size() < maxEntries; j++) {
            const uint32_t sp = be16(&d[idxt + 4 + 2 * j]);
            const uint32_t ep = j + 1 < ec ? be16(&d[idxt + 4 + 2 * (j + 1)]) : idxt;
            if (sp >= ep || ep > d.size()) continue;
            IndexEntry e;
            const uint32_t tl = d[sp];
            if (sp + 1 + tl > ep) continue;
            e.ident.assign((const char*)&d[sp + 1], tl);
            uint32_t p = sp + 1 + tl;
            std::vector<uint8_t> cb(d.begin() + p, d.begin() + std::min<uint32_t>(p + cbc, ep));
            p += cbc;
            size_t cbi = 0;
            struct PTag { uint8_t tag; uint32_t count, bytes; uint8_t values; };
            std::vector<PTag> pt;
            for (const TagX& x : tags) {
                if (x.eof) {
                    cbi++;
                    continue;
                }
                if (cbi >= cb.size()) break;
                uint32_t v = cb[cbi] & x.mask;
                if (!v) continue;
                if (v == x.mask) {
                    if (__builtin_popcount(x.mask) > 1) pt.push_back({x.tag, 0, decint(d.data(), ep, p), x.values});
                    else pt.push_back({x.tag, 1, 0, x.values});
                } else {
                    uint8_t m = x.mask;
                    while (!(m & 1)) {
                        m >>= 1;
                        v >>= 1;
                    }
                    pt.push_back({x.tag, v, 0, x.values});
                }
            }
            for (const PTag& x : pt) {
                std::vector<uint32_t> vals;
                if (x.count) {
                    for (uint32_t k = 0; k < x.count * x.values && p < ep; k++) vals.push_back(decint(d.data(), ep, p));
                } else {
                    const uint32_t end = std::min(ep, p + x.bytes);
                    while (p < end) vals.push_back(decint(d.data(), end, p));
                }
                e.tags.emplace_back(x.tag, vals);
            }
            t.entries.push_back(std::move(e));
        }
    }
    return true;
}

uint32_t base32(const char* s, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        const char c = (char)toupper((unsigned char)s[i]);
        if (c >= '0' && c <= '9') v = v * 32 + (uint32_t)(c - '0');
        else if (c >= 'A' && c <= 'V') v = v * 32 + (uint32_t)(c - 'A' + 10);
        else break;
    }
    return v;
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
    {"br", "br", K_VOID}, {"hr", "hr", K_VOID}, {"img", "img", K_VOID}, {"image", "img", K_VOID},
    {"section", "div", K_BLOCK}, {"article", "div", K_BLOCK}, {"aside", "div", K_BLOCK}, {"nav", "div", K_BLOCK},
    {"header", "div", K_BLOCK}, {"footer", "div", K_BLOCK}, {"main", "div", K_BLOCK}, {"figure", "div", K_BLOCK},
    {"figcaption", "div", K_BLOCK},
    {"head", "", K_SKIP}, {"script", "", K_SKIP}, {"style", "", K_SKIP}, {"title", "", K_SKIP},
};

const ElemInfo* elemInfo(const std::string& n) {
    for (const auto& e : kElems)
        if (n == e.in) return &e;
    return nullptr;
}

// Blocks that only hold other blocks: a file may end and the next begin inside them.
bool isContainer(const std::string& n) {
    return n == "div" || n == "blockquote" || n == "center" || n == "section" || n == "article" || n == "aside" ||
           n == "main" || n == "header" || n == "footer" || n == "nav";
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
    std::string openTag(const Tag& t, const ElemInfo& e, const std::string& extraStyle = std::string());
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
    Tag _pTag[kMaxStack];          // the start tag of each open p (pass 2): re-opened after a <br>
    std::string _openStr[kMaxStack];   // what each open element was written as (pass 2): re-opened in the next file
    uint32_t _pBytes = 0;          // text written into the open p (pass 2)
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

    // KF8 (AZW3): the book is "parts" (one XHTML file each), each a skeleton
    // with fragments put into it, in the text after the skeleton.
    bool _kf8 = false;
    struct Skel { uint32_t pos, len, frags; };
    struct Frag { uint32_t insert, len; };
    std::vector<Skel> _skels;
    std::vector<Frag> _frags;
    uint32_t _textEnd = 0;                       // end of the XHTML flow (FDST)
    std::vector<std::pair<uint32_t, uint32_t>> _flows;   // CSS flows: [start, end)
    std::vector<int> _cssFlows;                  // flow numbers written as CSS
    struct KfToc {
        uint32_t target = 0;
        uint8_t depth = 0;
        std::string label;
    };
    std::vector<KfToc> _kfToc;
    // decompressed text records, two at a time (skeleton and fragment reads alternate)
    uint32_t _cacheRec[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t _cacheLen[2] = {0, 0};
    uint8_t _cache[2][8192];
    int _cacheNext = 0;
    bool readText(uint32_t pos, uint8_t* dst, uint32_t n);
    bool feedText(uint32_t pos, uint32_t n, uint32_t asmPos);
    bool loadKf8();
    uint32_t kindlePos(const char* v) const;
    std::string kindleLink(const char* v);
    bool canSplitHere() const;
    bool textBlockOnTop() const;
    void continueBlock();

    // KF8 as the web reader (foliate-js) reads it: each part is a section of its
    // own, parsed as XHTML. Its markup is kept as it is (only kindle: links and
    // images are pointed at the EPUB's files), so a CFI means the same place
    // in both; a part longer than kHardSplit goes on in more files, a long text
    // run is cut in pieces in one <div> for the engine, and the CFI map says
    // where those stand (core/cfi_map.h, "cfimap 2").
    struct KfOpen {
        std::string name;       // as written
        std::string open;       // its start tag, without its id (written again in a new file or piece)
        uint16_t folIdx = 0, folCount = 0, ourIdx = 0, ourCount = 0;
        uint32_t slot = 0;      // UTF-16 units of its text since its last child element
        uint32_t ordinal = 0;
        bool wrapped = false;   // cut in pieces
        bool container = false;
        uint16_t piece = 0;     // a cut element: the piece being written
        std::string pieces;     // its line of the map
    };
    std::vector<KfOpen> _kfStack;              // open elements below <body>
    uint16_t _kfBodyFol = 0, _kfBodyOur = 0;   // <body>'s children so far
    std::string _kfBodyTag;
    int _kfSection = -1;
    std::string _kfPendingF;                   // the map's line for the file about to start
    uint32_t _kfOrdinal = 0;
    std::vector<uint32_t> _kfLong;             // pass 1: elements whose text is cut
    size_t _kfLongNext = 0;
    std::string _cfiMap;
    void kfPart(int k, uint32_t pos);
    void kfSplit(uint32_t offset);
    void kfTag(const std::string& raw, uint32_t offset);
    void kfText(const char* s, uint32_t n);
    std::string kfRewrite(const std::string& raw, const std::string& id, bool dropId);
    void kfNewFileLine(const std::string& offsets);
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
    _kf8 = _h.version >= 8 && _h.kf8Boundary == 0xFFFFFFFF;   // AZW3 (a MOBI 6 + KF8 file is read as MOBI 6)
    if (_h.compression != 1 && _h.compression != 2) {
        _stats.error = _h.compression == 17480 ? "HUFF/CDIC compression" : "unknown compression";
        return ConvertResult::Unsupported;
    }
    if (_h.textRecords == 0 || _h.textRecords >= _db.count()) return ConvertResult::ParseError;
    if (_kf8 && !loadKf8()) {
        if (_stats.error.empty()) _stats.error = "KF8 tables unreadable";
        return ConvertResult::ParseError;
    }
    if (_h.encoding != 65001) {   // the title in the header is in the book's code page too
        _h.title = toUtf8(_h.title);
        for (auto& a : _h.authors) a = toUtf8(a);
        _h.description = toUtf8(_h.description);
    }

    ConvertResult r = pass(1);
    if (r != ConvertResult::Ok) return r;
    for (const KfToc& t : _kfToc) _targets.push_back(t.target);
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
    // KF8: the book's own style sheets (flows 1.. that are not SVG).
    for (size_t f = 0; f < _flows.size(); f++) {
        const uint32_t a = _flows[f].first, b = _flows[f].second;
        if (b <= a) continue;
        uint8_t head[8];
        const uint32_t hn = std::min<uint32_t>(8, b - a);
        if (!readText(a, head, hn)) continue;
        uint32_t k = 0;
        while (k < hn && isspace(head[k])) k++;
        if (k < hn && head[k] == '<') continue;   // SVG
        char nm[32];
        snprintf(nm, sizeof(nm), "OEBPS/flow%u.css", (unsigned)(f + 1));
        _zip.open(nm);
        for (uint32_t at = a; at < b;) {
            const uint32_t m = std::min<uint32_t>(b - at, sizeof(_plain));
            if (!readText(at, _plain, m)) break;
            _zip.write(_plain, m);
            at += m;
        }
        _zip.close();
        _cssFlows.push_back((int)f + 1);
    }
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
    if (_kf8 && !_cfiMap.empty()) _stats.cfiMap = "cfimap 2\n" + _cfiMap;
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
    if (_kf8) {
        // Each part: its skeleton with its fragments put in, as one stream of
        // positions in the assembled text (which are the text's own positions).
        uint32_t fragAt = 0;
        _kfStack.clear();
        _kfOrdinal = 0;
        _kfLongNext = 0;
        _kfSection = -1;
        _cfiMap.clear();
        for (size_t k = 0; k < _skels.size(); k++) {
            const Skel& sk = _skels[k];
            // a part is a file of its own (a foliate section)
            if (!_text.empty()) onText(_text.data(), (uint32_t)_text.size(), _textStart);
            _text.clear();
            _tstate = T_TEXT;
            _inBody = false;
            _skip = 0;
            kfPart((int)k, sk.pos);
            uint32_t skelAt = sk.pos, asmPos = sk.pos, base = sk.pos + sk.len;
            for (uint32_t j = 0; j < sk.frags && fragAt < _frags.size(); j++, fragAt++) {
                const Frag& fr = _frags[fragAt];
                const uint32_t insert = std::max(fr.insert, asmPos);
                const uint32_t before = std::min(insert - asmPos, sk.pos + sk.len - skelAt);
                if (!feedText(skelAt, before, asmPos)) return ConvertResult::IoError;
                skelAt += before;
                asmPos += before;
                if (!feedText(base, fr.len, asmPos)) return ConvertResult::IoError;
                base += fr.len;
                asmPos += fr.len;
            }
            if (!feedText(skelAt, sk.pos + sk.len - skelAt, asmPos)) return ConvertResult::IoError;
            if (_progress) {
                const int pm = (which == 1 ? 0 : 300) + (int)((which == 1 ? 300 : 500) * (uint64_t)(k + 1) / _skels.size());
                if (pm / 10 != _lastPermille / 10) {
                    _lastPermille = pm;
                    _progress(_user, pm);
                }
            }
            if (_zip.failed()) return ConvertResult::IoError;
        }
        if (!_text.empty()) onText(_text.data(), (uint32_t)_text.size(), _textStart);
        _text.clear();
        closeTo(0);
        return ConvertResult::Ok;
    }
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
        "</title><link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/>";
    emit(head.data(), (uint32_t)head.size());
    for (int f : _cssFlows) {
        char l[96];
        snprintf(l, sizeof(l), "<link rel=\"stylesheet\" type=\"text/css\" href=\"flow%d.css\"/>", f);
        emit(l);
    }
    if (_kf8) {
        emit("</head>");
        emit(_kfBodyTag.empty() ? "<body>" : _kfBodyTag.c_str());
        _cfiMap += _kfPendingF;   // this file in the CFI map
        _kfPendingF.clear();
    } else {
        emit("</head><body>\n");
    }
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
    // The open containers (a wrapping div...) go on in the next file.
    const int keep = _depth;
    std::vector<std::string> names(_stack, _stack + keep), opens(_openStr, _openStr + keep);
    std::vector<float> ems(_em, _em + keep);
    closeTo(0);
    closeChapter();
    _file++;
    _fileHasContent = false;
    _fileStart = offset;
    if (_pass == 1) _bounds.push_back(offset);
    for (int d = 0; d < keep; d++) {
        _stack[d] = names[d];
        _openStr[d] = opens[d];
        _em[d] = ems[d];
        if (_pass == 2) emit(opens[d].data(), (uint32_t)opens[d].size());
    }
    _depth = keep;
}

bool MobiConverter::canSplitHere() const {
    for (int d = 0; d < _depth; d++)
        if (!isContainer(_stack[d])) return false;
    return !_pending;
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
        if (e && e->kind == K_BLOCK) _pBytes = 0;   // the text after it is a new engine paragraph
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

std::string MobiConverter::openTag(const Tag& t, const ElemInfo& e, const std::string& extraStyle) {
    if (_pass != 2) return std::string();
    std::string o = "<";
    o += e.out;
    auto attr = [&](const char* name, const std::string& value) {
        o += ' ';
        o += name;
        o += "=\"";
        o += xmlEscape(value);
        o += '"';
    };
    std::string cls = t.get("class") ? toUtf8(t.get("class")) : std::string();
    if (t.name == "center") cls = cls.empty() ? "center" : cls + " center";
    if (!cls.empty()) attr("class", cls);
    std::string st = styleFor(t, t.name) + extraStyle;
    if (const char* own = t.get("style")) st = toUtf8(own) + (st.empty() || toUtf8(own).empty() ? "" : ";") + st;
    if (!st.empty()) attr("style", st);
    if (const char* id = t.get("id")) attr("id", toUtf8(id));
    for (const char* a : {"colspan", "rowspan"}) {
        if (const char* v = t.get(a)) attr(a, std::to_string(atoi(v)));
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
            href = strncmp(h, "kindle:", 7) == 0 ? kindleLink(h) : toUtf8(h);
        }
        if (!href.empty()) attr("href", href);
    }
    if (e.kind == K_VOID && (t.name == "img" || t.name == "image")) {
        int idx = -1;
        if (const char* ri = t.get("recindex")) {
            idx = imageRef((uint32_t)strtoul(ri, nullptr, 10));
        } else {
            const char* src = t.get(t.name == "image" ? "href" : "src");
            if (src && strncmp(src, "kindle:embed:", 13) == 0) {
                const char* v = src + 13;
                size_t n = 0;
                while (v[n] && isalnum((unsigned char)v[n])) n++;
                idx = imageRef(base32(v, n));
            }
        }
        char b[32];
        snprintf(b, sizeof(b), "images/i%04d", idx < 0 ? 0 : idx);
        attr("src", b);
        attr("alt", "");
        for (const char* a : {"width", "height"}) {
            if (const char* v = t.get(a)) {
                if (atoi(v) > 0) attr(a, std::to_string(atoi(v)));
            }
        }
    }
    o += e.kind == K_VOID ? "/>" : ">";
    emit(o.data(), (uint32_t)o.size());
    return o;
}

// "kindle:pos:fid:XXXX:off:YYYYYYYYYY": a position in the assembled text.
uint32_t MobiConverter::kindlePos(const char* v) const {
    const char* f = strstr(v, "fid:");
    const char* o = strstr(v, "off:");
    if (!f || !o) return UINT32_MAX;
    f += 4;
    o += 4;
    size_t fn = 0, on = 0;
    while (isalnum((unsigned char)f[fn])) fn++;
    while (isalnum((unsigned char)o[on])) on++;
    const uint32_t fid = base32(f, fn), off = base32(o, on);
    if (fid >= _frags.size()) return UINT32_MAX;
    return _frags[fid].insert + off;
}

std::string MobiConverter::kindleLink(const char* v) {
    const uint32_t target = kindlePos(v);
    if (target == UINT32_MAX) return std::string();
    const uint16_t f = fileOf(target);
    char b[48];
    if (f == _file) snprintf(b, sizeof(b), "#fp%u", (unsigned)target);
    else snprintf(b, sizeof(b), "c%04u.xhtml#fp%u", (unsigned)f, (unsigned)target);
    return b;
}

void MobiConverter::onTag(const std::string& raw, uint32_t offset) {
    if (raw.empty() || raw[0] == '!' || raw[0] == '?') return;   // comments, doctype
    if (_kf8) {
        kfTag(raw, offset);
        return;
    }
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
        const char* h = t.get("href");
        if (h && strncmp(h, "kindle:pos:", 11) == 0) {
            const uint32_t target = kindlePos(h);
            if (target != UINT32_MAX) _targets.push_back(target);
        }
    }

    if (!e) return;   // unknown tags (mbp:*, o:p, ...) go, their text stays
    if (_pending) {
        if (!t.end && t.name == "font" && fontEm(t) > 0) {
            // the block's first content: its size becomes the block's
            const float em = fontEm(t);
            flushPending(em);
            if (_depth < kMaxStack) {
                _em[_depth] = 0;
                _openStr[_depth] = "<span>";
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
    if (e->kind == K_BLOCK || e->kind == K_VOID) {
        // A block closes the inline elements and paragraph it was left inside.
        while (_depth > 0 && e->kind == K_BLOCK) {
            const ElemInfo* top = elemInfo(_stack[_depth - 1]);
            if ((top && top->kind == K_INLINE) || isAutoCloser(t.name, _stack[_depth - 1])) closeTo(_depth - 1);
            else break;
        }
        // Past the size limit, between blocks, a new file (wrapping divs go on in it).
        if (_pass == 1 && offset - _fileStart > kHardSplit && canSplitHere()) boundary(offset);
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
            _openStr[_depth].clear();
            _stack[_depth++] = t.name;
        }
        return;
    }
    // Many MOBI books (royallib, ...) put a whole chapter in one <p>, with <br>
    // between its paragraphs: tens of KB that the engine would have to cut
    // mid-word. A <br> straight inside a p (no inline element open, no size
    // moved onto it) closes it and starts the next one, without the space above.
    // (KF8 books come from EPUBs with real paragraphs: their <br> stays, a p's
    // class margins on every line would space a poem out.)
    if (t.name == "br" && !_kf8 && _pass == 2 && _depth > 0 && _stack[_depth - 1] == "p" && _em[_depth - 1] == 0) {
        Tag next = _pTag[_depth - 1];
        for (auto it = next.attrs.begin(); it != next.attrs.end();)
            it = (it->first == "height" || it->first == "id") ? next.attrs.erase(it) : it + 1;
        emit("</p>\n");
        openTag(next, *elemInfo("p"));
        _pBytes = 0;
        return;
    }
    // A block that has collected a good part of the engine's paragraph buffer
    // goes on in a new one at its next <br> (a line break either way).
    if (t.name == "br" && _pass == 2 && _pBytes > 600 && textBlockOnTop()) {
        continueBlock();
        return;
    }
    std::string extra;
    if (t.name == "font") {
        const float em = fontEm(t), base = blockEm();
        if (em > 0 && (em / base > 1.01f || em / base < 0.99f)) extra = emStyle(em / base);
    }
    const std::string written = openTag(t, *e, extra);
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
        _openStr[_depth] = written;
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
    const std::string written = openTag(_pendingTag, *_pendingInfo, extra);
    if (d >= 0) _openStr[d] = written;
}

void MobiConverter::onText(const char* s, uint32_t n, uint32_t offset) {
    if (_skip > 0 || !_inBody) return;
    if (_kf8) {
        kfText(s, n);
        return;
    }
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
    while (_pass == 2 && textBlockOnTop() && _pBytes + n > kParLimit) {
        const uint32_t room = _pBytes < kParLimit ? kParLimit - _pBytes : 0;
        uint32_t cut = room < n ? room : n;
        // the end of a sentence in the last ~800 bytes reads best, else a space
        uint32_t sentence = cut;
        while (sentence > 1 && cut - sentence < 800 &&
               !(s[sentence - 1] == ' ' && (s[sentence - 2] == '.' || s[sentence - 2] == '!' || s[sentence - 2] == '?')))
            sentence--;
        if (sentence > 1 && cut - sentence < 800) cut = sentence;
        while (cut > 0 && s[cut - 1] != ' ') cut--;
        if (cut == 0) {   // no space within the room: the first one after it
            cut = room;
            while (cut < n && s[cut] != ' ') cut++;
            if (cut >= n) break;
            cut++;
        }
        emitText(s, cut);
        continueBlock();
        s += cut;
        n -= cut;
    }
    _pBytes += n;
    emitText(s, n);
}


// The innermost open element is a block holding text (no inline element open
// inside it, no font size moved onto it): it can end and go on in a new one.
bool MobiConverter::textBlockOnTop() const {
    if (_depth == 0 || _pending || _em[_depth - 1] != 0) return false;
    const std::string& n = _stack[_depth - 1];
    return n == "p" || n == "div" || n == "blockquote" || n == "li" || n == "dd" || n == "center" || n == "section";
}

// Ends the block on top and opens it again (no space above, no first-line
// indent, no id): the text reads on as before, in a new engine paragraph.
void MobiConverter::continueBlock() {
    const int d = _depth - 1;
    const ElemInfo* e = elemInfo(_stack[d]);
    if (!e || _pass != 2) return;
    std::string o = _openStr[d];
    const size_t idAt = o.find(" id=\"");
    if (idAt != std::string::npos) o.erase(idAt, o.find('"', idAt + 5) + 1 - idAt);
    const size_t st = o.find(" style=\"");
    if (st != std::string::npos) o.insert(st + 8, "margin-top:0;text-indent:0;");
    else o.insert(o.size() - 1, " style=\"margin-top:0;text-indent:0;\"");
    emit("</");
    emit(e->out);
    emit(">\n");
    emit(o.data(), (uint32_t)o.size());
    _pBytes = 0;
}

// ------------------------------------------------------------- KF8 as is

namespace {
// Characters of XHTML text as the DOM has them (UTF-16 units; an entity is one).
uint32_t utf16Units(const char* s, uint32_t n) {
    uint32_t u = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t c = (uint8_t)s[i];
        if (c == '&') {
            uint32_t k = i + 1;
            while (k < n && k - i < 12 && (isalnum((unsigned char)s[k]) || s[k] == '#')) k++;
            if (k < n && s[k] == ';' && k > i + 1) {
                // &#x1F600; and the like outside the BMP are two units
                const bool hex = s[i + 1] == '#' && (s[i + 2] == 'x' || s[i + 2] == 'X');
                const unsigned long cp = s[i + 1] == '#' ? strtoul(s + i + (hex ? 3 : 2), nullptr, hex ? 16 : 10) : 0;
                u += cp > 0xFFFF ? 2 : 1;
                i = k;
                continue;
            }
        }
        if ((c & 0xC0) == 0x80) continue;
        u += c >= 0xF0 ? 2 : 1;
    }
    return u;
}

// What the engine starts a paragraph at (as EngineTextCounter / ChapterLayout).
bool isEngineBlock(const std::string& n) {
    static const char* const k[] = {"p", "h1", "h2", "h3", "h4", "h5", "h6", "blockquote", "li", "div", "section",
                                    "article", "figure", "aside", "figcaption", "ul", "ol", "table", "tr", "td",
                                    "th", "dt", "dd"};
    for (const char* b : k)
        if (n == b) return true;
    return false;
}
}  // namespace

void MobiConverter::kfNewFileLine(const std::string& offsets) {
    if (_pass == 2)
        _kfPendingF = "F " + std::to_string(_file) + " " + std::to_string(_kfSection) + offsets + "\n";
}

// Part k starts: a new file (once the last one has anything).
void MobiConverter::kfPart(int k, uint32_t pos) {
    while (!_kfStack.empty()) {   // (a part that did not close its elements)
        if (_pass == 2) emit(("</" + _kfStack.back().name + ">").c_str());
        _kfStack.pop_back();
    }
    if (_fileHasContent) {
        closeChapter();
        _file++;
        _fileHasContent = false;
        if (_pass == 1) _bounds.push_back(pos);
    }
    _fileStart = pos;
    _kfSection = k;
    _kfBodyFol = _kfBodyOur = 0;
    _kfBodyTag.clear();
    _pBytes = 0;
    kfNewFileLine("");
}

// A new file inside the part, between the children of the open containers:
// they are opened again in it; the map gets where they and the next child
// stand among foliate's.
void MobiConverter::kfSplit(uint32_t offset) {
    std::string offs;
    for (const KfOpen& o : _kfStack) offs += " " + std::to_string(o.folIdx);
    offs += " " + std::to_string(_kfStack.empty() ? _kfBodyFol : _kfStack.back().folCount);
    for (size_t i = _kfStack.size(); i-- > 0;)
        if (_pass == 2) emit(("</" + _kfStack[i].name + ">").c_str());
    closeChapter();
    _file++;
    if (_pass == 1) _bounds.push_back(offset);
    _fileStart = offset;
    _fileHasContent = true;
    kfNewFileLine(offs);
    _kfBodyOur = _kfStack.empty() ? 0 : 1;
    for (size_t i = 0; i < _kfStack.size(); i++) {
        KfOpen& o = _kfStack[i];
        if (_pass == 2) emit(o.open.c_str());
        o.ourIdx = 0;
        o.ourCount = i + 1 < _kfStack.size() ? 1 : 0;
        o.slot = 0;
    }
}

// The start tag again, with kindle: links and images pointed at the EPUB's
// files, and the id given (`id`), left out (`dropId`) or kept.
std::string MobiConverter::kfRewrite(const std::string& raw, const std::string& id, bool dropId) {
    std::string o = "<";
    size_t i = 0;
    const size_t n = raw.size();
    while (i < n && !isspace((unsigned char)raw[i]) && raw[i] != '/' && raw[i] != '>') o += raw[i++];
    bool selfClose = false;
    while (i < n) {
        while (i < n && (isspace((unsigned char)raw[i]) || raw[i] == '/')) {
            if (raw[i] == '/') selfClose = true;
            i++;
        }
        if (i >= n) break;
        selfClose = false;
        std::string an;
        while (i < n && !isspace((unsigned char)raw[i]) && raw[i] != '=' && raw[i] != '/') an += raw[i++];
        while (i < n && isspace((unsigned char)raw[i])) i++;
        std::string av;
        char q = '"';
        bool hasValue = false;
        if (i < n && raw[i] == '=') {
            hasValue = true;
            i++;
            while (i < n && isspace((unsigned char)raw[i])) i++;
            if (i < n && (raw[i] == '"' || raw[i] == '\'')) {
                q = raw[i++];
                while (i < n && raw[i] != q) av += raw[i++];
                i++;
            } else {
                while (i < n && !isspace((unsigned char)raw[i])) av += raw[i++];
                if (!av.empty() && av.back() == '/' && i >= n) {
                    av.pop_back();
                    selfClose = true;
                }
                if (av.find('"') != std::string::npos) q = '\'';
            }
        }
        if (an.empty()) continue;
        std::string lower = an;
        for (char& c : lower) c = (char)tolower(c);
        if (lower == "id" && (dropId || !id.empty())) continue;
        if (strncmp(av.c_str(), "kindle:embed:", 13) == 0) {
            const char* v = av.c_str() + 13;
            size_t k = 0;
            while (v[k] && isalnum((unsigned char)v[k])) k++;
            const int idx = imageRef(base32(v, k));
            char b[32];
            snprintf(b, sizeof(b), "images/i%04d", idx < 0 ? 0 : idx);
            av = b;
        } else if (strncmp(av.c_str(), "kindle:pos:", 11) == 0) {
            av = kindleLink(av.c_str());
        }
        o += ' ';
        o += an;
        if (hasValue) {
            o += '=';
            o += q;
            o += av;
            o += q;
        }
    }
    if (!id.empty()) o += " id=\"" + id + "\"";
    o += selfClose ? "/>" : ">";
    return o;
}

void MobiConverter::kfTag(const std::string& raw, uint32_t offset) {
    Tag t;
    parseTag(raw, t);
    if (t.name.empty()) return;
    if (t.name == "head") {   // ours is written instead
        if (t.end) {
            if (_skip > 0) _skip--;
        } else if (!t.selfClose) {
            _skip++;
        }
        return;
    }
    if (_skip > 0 || t.name == "html") return;
    if (t.name == "body") {
        if (!t.end) {
            _inBody = true;
            _kfBodyTag = "<" + raw + ">";
            if (_kfBodyTag.size() > 2 && _kfBodyTag[_kfBodyTag.size() - 2] == '/') _kfBodyTag.erase(_kfBodyTag.size() - 2, 1);
        } else {
            while (!_kfStack.empty()) {
                if (_pass == 2) emit(("</" + _kfStack.back().name + ">").c_str());
                _kfStack.pop_back();
            }
            _inBody = false;
        }
        return;
    }
    if (!_inBody) return;
    std::string name;
    for (size_t i = 0; i < raw.size() && !isspace((unsigned char)raw[i]) && raw[i] != '/' && raw[i] != '>'; i++)
        name += raw[i];
    if (t.end) {
        std::string en = raw.substr(1);
        while (!en.empty() && isspace((unsigned char)en.back())) en.pop_back();
        // d < size() in the loop, and size() > d >= 0 makes back() valid (cppcheck can't see it).
        for (size_t d = _kfStack.size(); d-- > 0;) {
            // cppcheck-suppress containerOutOfBounds
            if (_kfStack[d].name != en) continue;
            while (_kfStack.size() > d) {
                // cppcheck-suppress containerOutOfBounds
                KfOpen& o = _kfStack.back();
                if (_pass == 2) emit(("</" + o.name + ">").c_str());
                if (o.wrapped && _pass == 2) {
                    emit("</div>");
                    _cfiMap += o.pieces + "\n";
                }
                if (isEngineBlock(t.name)) _pBytes = 0;
                _kfStack.pop_back();
                if (!_kfStack.empty()) _kfStack.back().slot = 0;   // a new text run after this child
            }
            break;
        }
        return;
    }
    // Contents and links: the places they point at (pass 1).
    if (_pass == 1 && t.name == "a") {
        const char* h = t.get("href");
        if (h && strncmp(h, "kindle:pos:", 11) == 0) {
            const uint32_t target = kindlePos(h);
            if (target != UINT32_MAX) _targets.push_back(target);
        }
    }
    bool container = isContainer(t.name) || t.name == "body";
    for (const KfOpen& o : _kfStack) container = container && o.container && !o.wrapped;
    if (offset - _fileStart > kHardSplit && _fileHasContent && container) kfSplit(offset);

    KfOpen o;
    o.name = name;
    o.container = isContainer(t.name);
    if (_kfStack.empty()) {
        o.folIdx = _kfBodyFol++;
        o.ourIdx = _kfBodyOur++;
    } else {
        KfOpen& p = _kfStack.back();
        o.folIdx = p.folCount++;
        o.ourIdx = p.ourCount++;
        p.slot = 0;
    }
    o.ordinal = ++_kfOrdinal;
    _fileHasContent = true;
    if (isEngineBlock(t.name)) _pBytes = 0;
    // A place a link or the contents point at: this tag's id.
    std::string id;
    while (_nextTarget < _targets.size() && _targets[_nextTarget] <= offset) {
        if (id.empty()) id = "fp" + std::to_string(_targets[_nextTarget]);
        _nextTarget++;
    }
    while (_kfLongNext < _kfLong.size() && _kfLong[_kfLongNext] < o.ordinal) _kfLongNext++;
    o.wrapped = _pass == 2 && _kfLongNext < _kfLong.size() && _kfLong[_kfLongNext] == o.ordinal;
    if (_pass == 2) {
        if (o.wrapped) {
            std::string path = "4";
            for (const KfOpen& a : _kfStack) {
                path += "/" + std::to_string(2 * (a.ourIdx + 1));
                if (a.wrapped) path += "/" + std::to_string(2 * (a.piece + 1));   // inside a piece of a cut one
            }
            path += "/" + std::to_string(2 * (o.ourIdx + 1));
            o.pieces = "L " + std::to_string(_file) + " " + path + " 0 0";
            emit("<div class=\"lp\">");
        }
        emit(kfRewrite(raw, id, false).c_str());
        o.open = kfRewrite(raw, std::string(), true);
    }
    if (t.selfClose) {
        if (o.wrapped && _pass == 2) {
            emit("</div>");
            _cfiMap += o.pieces + "\n";
        }
        return;
    }
    _kfStack.push_back(o);
}

void MobiConverter::kfText(const char* s, uint32_t n) {
    bool blank = true;
    for (uint32_t i = 0; i < n && blank; i++)
        if (!isspace((unsigned char)s[i])) blank = false;
    if (!blank) {
        _fileHasContent = true;
        if (_pass == 2) {
            if (_fileLabels.size() < _file) _fileLabels.resize(_file);
            std::string& l = _fileLabels[_file - 1];
            if (l.size() < 96) {
                if (!l.empty() && l.back() != ' ') l += ' ';
                appendCollapsed(l, s, n, 96);
            }
        }
    }
    _pBytes += n;
    // The engine holds ~3 KB of a paragraph and cuts a longer one wherever it is
    // full: a long run goes on in a new copy of its element at a space (pass 1
    // finds the elements, pass 2 cuts them, in a div standing for the one).
    constexpr uint32_t kParLimit = 2400;
    while (!_kfStack.empty() && _pBytes > kParLimit) {
        KfOpen& top = _kfStack.back();
        const uint32_t before = _pBytes - n;
        const uint32_t room = before < kParLimit ? kParLimit - before : 0;
        uint32_t cut = room < n ? room : n;
        uint32_t sentence = cut;
        while (sentence > 1 && cut - sentence < 800 &&
               !(s[sentence - 1] == ' ' && (s[sentence - 2] == '.' || s[sentence - 2] == '!' || s[sentence - 2] == '?')))
            sentence--;
        if (sentence > 1 && cut - sentence < 800) cut = sentence;
        while (cut > 0 && s[cut - 1] != ' ') cut--;
        if (cut == 0) {
            cut = room;
            while (cut < n && s[cut] != ' ') cut++;
            if (cut >= n) break;
            cut++;
        }
        if (_pass == 1) {
            if (_kfLong.empty() || _kfLong.back() != top.ordinal) _kfLong.push_back(top.ordinal);
        } else if (top.wrapped) {
            emitText(s, cut);
            top.slot += utf16Units(s, cut);
            emit(("</" + top.name + ">").c_str());
            emit(top.open.c_str());
            top.pieces += " " + std::to_string(top.folCount) + " " + std::to_string(top.slot);
            top.piece++;
            top.ourCount = 0;
        } else {
            emitText(s, cut);
        }
        s += cut;
        n -= cut;
        _pBytes = n;
    }
    if (!_kfStack.empty()) _kfStack.back().slot += utf16Units(s, n);
    emitText(s, n);
}

// ------------------------------------------------------------------ KF8

// The text from `pos` (records of recordSize bytes each once decompressed).
bool MobiConverter::readText(uint32_t pos, uint8_t* dst, uint32_t n) {
    const uint32_t rs = _h.recordSize ? _h.recordSize : 4096;
    while (n) {
        const uint32_t rec = 1 + pos / rs, at = pos % rs;
        int slot = _cacheRec[0] == rec ? 0 : _cacheRec[1] == rec ? 1 : -1;
        if (slot < 0) {
            slot = _cacheNext;
            _cacheNext ^= 1;
            if (rec > _h.textRecords) return false;
            const int32_t m = _db.read(rec, _rec, sizeof(_rec));
            if (m < 0) return false;
            const uint32_t body = (uint32_t)m - trailingSize(_rec, (uint32_t)m, _h.extraFlags);
            if (_h.compression == 2) {
                _cacheLen[slot] = palmDoc(_rec, body, _cache[slot], sizeof(_cache[slot]));
            } else {
                _cacheLen[slot] = std::min<uint32_t>(body, sizeof(_cache[slot]));
                memcpy(_cache[slot], _rec, _cacheLen[slot]);
            }
            _cacheRec[slot] = rec;
        }
        if (at >= _cacheLen[slot]) return false;
        const uint32_t k = std::min(n, _cacheLen[slot] - at);
        memcpy(dst, _cache[slot] + at, k);
        dst += k;
        pos += k;
        n -= k;
    }
    return true;
}

// Feeds text [pos, pos+n) to the tokenizer as assembled positions from asmPos.
bool MobiConverter::feedText(uint32_t pos, uint32_t n, uint32_t asmPos) {
    while (n) {
        const uint32_t k = std::min<uint32_t>(n, sizeof(_plain));
        if (!readText(pos, _plain, k)) return false;
        feed(_plain, k, asmPos);
        pos += k;
        asmPos += k;
        n -= k;
    }
    return true;
}

bool MobiConverter::loadKf8() {
    // Where the XHTML ends and the style sheets are (FDST).
    _textEnd = _h.textLength;
    std::vector<uint8_t> r;
    if (_h.fdst != 0xFFFFFFFF && _h.fdst < _db.count() && readRecordVec(_db, _h.fdst, r) && r.size() >= 12 &&
        memcmp(r.data(), "FDST", 4) == 0) {
        const uint32_t count = be32(&r[8]);
        for (uint32_t i = 0; i < count && 12 + 8 * i + 8 <= r.size(); i++) {
            const uint32_t a = be32(&r[12 + 8 * i]), b = std::min(be32(&r[16 + 8 * i]), _h.textLength);
            if (i == 0) _textEnd = b;
            else _flows.emplace_back(a, b);
        }
    }
    IndexTable skel, frag;
    if (!readIndex(_db, _h.skel, skel) || !readIndex(_db, _h.frag, frag)) {
        _stats.error = "KF8 part tables unreadable";
        return false;
    }
    for (const auto& e : skel.entries) {
        const uint32_t pos = e.get(6, 0), len = e.get(6, 1), frags = e.get(1, 0, 0);
        if (pos == 0xFFFFFFFF || len == 0xFFFFFFFF) return false;
        _skels.push_back({pos, len, frags});
    }
    for (const auto& e : frag.entries) {
        const uint32_t len = e.get(6, 1);
        if (len == 0xFFFFFFFF) return false;
        _frags.push_back({(uint32_t)strtoul(e.ident.c_str(), nullptr, 10), len});
    }
    if (_skels.empty()) return false;
    // Contents (NCX): label, level, and where it points.
    IndexTable ncx;
    if (_h.ncx != 0xFFFFFFFF && readIndex(_db, _h.ncx, ncx)) {
        for (const auto& e : ncx.entries) {
            KfToc t;
            const uint32_t fid = e.get(6, 0), off = e.get(6, 1);
            t.target = fid != 0xFFFFFFFF && fid < _frags.size() ? _frags[fid].insert + (off == 0xFFFFFFFF ? 0 : off)
                                                                 : e.get(1, 0);
            if (t.target == 0xFFFFFFFF) continue;
            t.depth = (uint8_t)std::min<uint32_t>(e.get(4, 0, 0), 8);
            const uint32_t lo = e.get(3, 0);
            if (lo != 0xFFFFFFFF) t.label = ncx.text(lo);
            trim(t.label);
            if (!t.label.empty()) _kfToc.push_back(t);
        }
    }
    return true;
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
    if (_kf8 && _kfToc.size() > 1) {
        // nested by level (a level deeper than one past its parent is taken as one past)
        int open = 0;
        for (const KfToc& t : _kfToc) {
            const uint16_t f = fileOf(t.target);
            if (f > _maxFile) continue;
            const int d = std::min<int>(t.depth + 1, open + 1);
            while (open >= d) {
                _zip.write("</navPoint>\n", 12);
                open--;
            }
            char src[24];
            snprintf(src, sizeof(src), "c%04u.xhtml", (unsigned)f);
            order++;
            s = "<navPoint id=\"n" + std::to_string(order) + "\" playOrder=\"" + std::to_string(order) +
                "\"><navLabel><text>" + xmlEscape(t.label) + "</text></navLabel><content src=\"" + src + "#fp" +
                std::to_string(t.target) + "\"/>";
            _zip.write(s.data(), (uint32_t)s.size());
            open++;
        }
        while (open > 0) {
            _zip.write("</navPoint>\n", 12);
            open--;
        }
    } else if (!_kf8 && _tocCount > 1 && _tmp.beginRead()) {
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
    }
    if (order == 0 && _maxFile > 1) {
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
                c.resize(cut);
                c += "\xE2\x80\xA6";
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
    for (int f : _cssFlows) {
        snprintf(buf, sizeof(buf), "<item id=\"flow%d\" href=\"flow%d.css\" media-type=\"text/css\"/>\n", f, f);
        put(buf);
    }
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
