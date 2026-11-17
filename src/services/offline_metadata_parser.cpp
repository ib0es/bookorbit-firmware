#include <epub/ZipCatalog.h>
#include <expat.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cmath>

#include "offline_metadata.h"

namespace OfflineMetadata {
bool parseSeriesIndex(const String& text, double& value) {
    value = 0;
    if (!text.length()) return true;
    if (text.length() > 32) return false;
    bool dot = false, digit = false;
    for (size_t i = 0; i < text.length(); ++i) {
        const char c = text[i];
        if (c == '.' && !dot && digit && i + 1 < text.length())
            dot = true;
        else if (c >= '0' && c <= '9')
            digit = true;
        else
            return false;
    }
    const double n = strtod(text.c_str(), nullptr);
    if (!digit || !std::isfinite(n) || n > 1000000) return false;
    value = n;
    return true;
}
namespace {
using namespace freeink::book;
constexpr uint32_t kXmlLimit = 128 * 1024;
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }
uint16_t le16(const uint8_t* p) { return p[0] | uint16_t(p[1]) << 8; }
const char* local(const char* p) {
    const char* c = strrchr(p, ':');
    return c ? c + 1 : p;
}
String clean(std::string_view s) {
    std::string out;
    bool space = false;
    for (unsigned char c : s) {
        if (c <= 32 || c == 127) {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += char(c);
        if (out.size() >= 512) break;
    }
    // Never leave a truncated UTF-8 sequence.
    if (out.size() >= 512) {
        out.resize(512);
        size_t p = out.size() - 1;
        while (p && (uint8_t(out[p]) & 0xc0) == 0x80) --p;
        const uint8_t c = out[p];
        const size_t n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
        if (out.size() - p < n) out.resize(p);
    }
    for (size_t i = 0; i < out.size();) {
        const uint8_t c = out[i++];
        if (c < 128) continue;
        const unsigned n = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!n || n > out.size() - i) return "";
        uint32_t cp = c & ((1 << (6 - n)) - 1);
        for (unsigned j = 0; j < n; ++j) {
            if ((uint8_t(out[i]) & 0xc0) != 0x80) return "";
            cp = (cp << 6) | (uint8_t(out[i++]) & 63);
        }
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000))
            return "";
    }
    return String(out.c_str());
}
struct Xml {
    XML_Parser parser = nullptr;
    Record result;
    std::string text;
    String rootfile;
    int depth = 0, scope = 0, capture = 0, author = 0;
    unsigned elements = 0;
    bool fb2 = false, complete = false, failed = false;
    enum Field { None, Title, Creator, Name, Meta } field = None;
    struct Metadata {
        String id, property, refines, name, content;
    } pending;
    std::vector<Metadata> metadata;
    static String attr(const char** attrs, const char* name) {
        for (size_t i = 0; attrs[i]; i += 2)
            if (!strcmp(attrs[i], name)) return clean(attrs[i + 1]);
        return "";
    }
    void series() {
        String calibre, index;
        for (const auto& m : metadata) {
            if (m.name == "calibre:series" && !calibre.length()) calibre = m.content;
            if (m.name == "calibre:series_index" && !index.length()) index = m.content;
        }
        result.series = calibre;
        parseSeriesIndex(index, result.seriesIndex);
        for (const auto& collection : metadata) {
            if (collection.property != "belongs-to-collection" || !collection.id.length() ||
                !collection.content.length())
                continue;
            String type, position;
            for (const auto& refinement : metadata) {
                if (refinement.refines != "#" + collection.id) continue;
                if (refinement.property == "collection-type" && !type.length()) type = refinement.content;
                if (refinement.property == "group-position" && !position.length()) position = refinement.content;
            }
            if (type != "series") continue;
            result.series = collection.content;
            parseSeriesIndex(position, result.seriesIndex);
            break;
        }
        if (!result.series.length()) result.seriesIndex = 0;
    }
    void abort() {
        failed = true;
        XML_StopParser(parser, XML_FALSE);
    }
    static void start(void* u, const char* raw, const char** attrs) {
        Xml& x = *static_cast<Xml*>(u);
        const char* n = local(raw);
        if (++x.depth > 32 || ++x.elements > 2048) {
            x.abort();
            return;
        }
        if (!strcmp(n, "rootfile") && !x.rootfile.length()) {
            for (size_t i = 0; attrs[i]; i += 2)
                if (!strcmp(attrs[i], "full-path") && strlen(attrs[i + 1]) < 256) x.rootfile = attrs[i + 1];
        }
        if ((!x.fb2 && !strcmp(n, "metadata")) || (x.fb2 && !strcmp(n, "title-info"))) x.scope = x.depth;
        if (!x.scope) return;
        if (x.fb2 && x.depth == x.scope + 1 && !strcmp(n, "sequence") && !x.result.series.length()) {
            x.result.series = attr(attrs, "name");
            if (x.result.series.length()) parseSeriesIndex(attr(attrs, "number"), x.result.seriesIndex);
        }
        if (x.fb2 && !strcmp(n, "author") && !x.author && !x.result.author.length()) x.author = x.depth;
        Field f = None;
        if ((!strcmp(n, x.fb2 ? "book-title" : "title")) && !x.result.title.length())
            f = Title;
        else if (!x.fb2 && !strcmp(n, "creator") && !x.result.author.length())
            f = Creator;
        else if (x.fb2 && x.author &&
                 (!strcmp(n, "first-name") || !strcmp(n, "middle-name") || !strcmp(n, "last-name") ||
                  !strcmp(n, "nickname")))
            f = Name;
        if (!x.fb2 && !strcmp(n, "meta") && x.depth == x.scope + 1 && x.metadata.size() < 64) {
            x.pending = {attr(attrs, "id"), attr(attrs, "property"), attr(attrs, "refines"), attr(attrs, "name"),
                         attr(attrs, "content")};
            f = Meta;
        }
        if (f != None && !x.capture) {
            x.field = f;
            x.capture = x.depth;
            x.text.clear();
        }
    }
    static void end(void* u, const char* raw) {
        Xml& x = *static_cast<Xml*>(u);
        if (x.capture == x.depth) {
            const String s = clean(x.text);
            if (x.field == Title) x.result.title = s;
            if (x.field == Creator) x.result.author = s;
            if (x.field == Meta) {
                if (x.pending.property.length()) x.pending.content = s;
                x.metadata.push_back(x.pending);
            }
            if (x.field == Name && s.length()) {
                if (x.result.author.length()) x.result.author += " ";
                if (x.result.author.length() + s.length() <= 512) x.result.author += s;
            }
            x.capture = 0;
            x.field = None;
        }
        if (x.author == x.depth) x.author = 0;
        if ((x.fb2 && !strcmp(local(raw), "description")) || (!x.fb2 && x.scope == x.depth)) {
            if (!x.fb2) x.series();
            x.complete = true;
            XML_StopParser(x.parser, XML_FALSE);
        }
        if (x.scope == x.depth) x.scope = 0;
        --x.depth;
    }
    static void chars(void* u, const char* s, int n) {
        Xml& x = *static_cast<Xml*>(u);
        if (x.capture && x.text.size() < 516) x.text.append(s, std::min<size_t>(n, 516 - x.text.size()));
    }
    static void doctype(void* u, const char*, const char*, const char*, int) { static_cast<Xml*>(u)->abort(); }
    static int encoding(void*, const char* name, XML_Encoding* info) {
        String lower(name);
        lower.toLowerCase();
        if (lower != "windows-1251" && lower != "cp1251") return XML_STATUS_ERROR;
        static const int high[64] = {0x402,  0x403,  0x201a, 0x453,  0x201e, 0x2026, 0x2020, 0x2021, 0x20ac, 0x2030,
                                     0x409,  0x2039, 0x40a,  0x40c,  0x40b,  0x40f,  0x452,  0x2018, 0x2019, 0x201c,
                                     0x201d, 0x2022, 0x2013, 0x2014, -1,     0x2122, 0x459,  0x203a, 0x45a,  0x45c,
                                     0x45b,  0x45f,  0xa0,   0x40e,  0x45e,  0x408,  0xa4,   0x490,  0xa6,   0xa7,
                                     0x401,  0xa9,   0x404,  0xab,   0xac,   0xad,   0xae,   0x407,  0xb0,   0xb1,
                                     0x406,  0x456,  0x491,  0xb5,   0xb6,   0xb7,   0x451,  0x2116, 0x454,  0xbb,
                                     0x458,  0x405,  0x455,  0x457};
        for (int i = 0; i < 256; ++i) info->map[i] = i < 128 ? i : i < 192 ? high[i - 128] : 0x410 + i - 192;
        info->data = nullptr;
        info->convert = nullptr;
        info->release = nullptr;
        return XML_STATUS_OK;
    }
    ~Xml() {
        if (parser) XML_ParserFree(parser);
    }
    bool open() {
        parser = XML_ParserCreate(nullptr);
        if (!parser) return false;
        XML_SetUserData(parser, this);
        XML_SetElementHandler(parser, start, end);
        XML_SetCharacterDataHandler(parser, chars);
        XML_SetStartDoctypeDeclHandler(parser, doctype);
        XML_SetUnknownEncodingHandler(parser, encoding, nullptr);
        XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_NEVER);
        return true;
    }
    bool feed(const char* p, int n, bool final) {
        const auto st = XML_Parse(parser, p, n, final);
        return !failed && (st != XML_STATUS_ERROR || complete);
    }
};
bool findEntry(BookSource& src, const char* name, ZipEntry& e, Cancelled cancel) {
    uint32_t off = 0, bytes = 0;
    uint16_t count = 0;
    if (ZipCatalog::locateCentralDirectory(src, &off, &bytes, &count) != BookStatus::Ok || count > 2048 ||
        bytes > 256 * 1024)
        return false;
    const uint64_t end = uint64_t(off) + bytes;
    for (unsigned i = 0; i < count; ++i) {
        if (cancel && cancel()) return false;
        uint8_t h[46];
        if (uint64_t(off) + 46 > end || src.readAt(off, h, 46) != 46 || le32(h) != 0x02014b50) return false;
        const uint32_t len = le16(h + 28), step = 46 + len + le16(h + 30) + le16(h + 32);
        if (uint64_t(off) + step > end) return false;
        char path[256] = {};
        if (len < sizeof(path) && src.readAt(off + 46, path, len) == int32_t(len) && !strcmp(path, name)) {
            if (le16(h + 8) & 1) return false;
            e.compressedSize = le32(h + 20);
            e.uncompressedSize = le32(h + 24);
            e.method = le16(h + 10);
            e.localHeaderOffset = le32(h + 42);
            return e.uncompressedSize <= kXmlLimit && e.compressedSize <= kXmlLimit;
        }
        off += step;
    }
    return false;
}
bool xmlEntry(BookSource& src, const ZipEntry& e, Xml& xml, Cancelled cancel) {
    const size_t cap = e.method == 0 ? 1024 : 56 * 1024;
    std::unique_ptr<uint8_t, decltype(&free)> mem(static_cast<uint8_t*>(malloc(cap)), free);
    if (!mem || !xml.open()) return false;
    Arena arena(mem.get(), cap);
    ZipEntryReader reader;
    if (reader.open(src, e, arena) != BookStatus::Ok) return false;
    char buf[1024];
    uint32_t total = 0;
    for (;;) {
        if (cancel && cancel()) return false;
        const int n = reader.read(buf, sizeof(buf));
        if (n < 0 || total + n > kXmlLimit || !xml.feed(buf, std::max(0, n), n == 0)) return false;
        total += n;
        if (!n || xml.complete) return true;
    }
}
Record mobi(BookSource& src, Cancelled cancel) {
    Record r;
    uint8_t h[96];
    if (src.readAt(0, h, sizeof(h)) != sizeof(h) || memcmp(h + 60, "BOOKMOBI", 8) ||
        ((unsigned(h[76]) << 8) | h[77]) < 2)
        return r;
    const uint32_t off = be32(h + 78), end = be32(h + 86);
    if (end <= off || end > src.size() || end - off > kXmlLimit || end - off < 148) return r;
    std::unique_ptr<uint8_t, decltype(&free)> mem(static_cast<uint8_t*>(malloc(end - off)), free);
    if (!mem || (cancel && cancel()) || src.readAt(off, mem.get(), end - off) != int32_t(end - off)) return r;
    const uint8_t* p = mem.get();
    const uint32_t size = end - off;
    if (memcmp(p + 16, "MOBI", 4) || p[12] || p[13]) return r;  // encrypted books: no guesses
    const uint32_t header = be32(p + 20);
    if (header < 116 || header > size - 16) return r;
    const uint32_t encoding = be32(p + 28);
    if (encoding != 65001 && encoding != 1252) return r;
    auto text = [&](uint32_t at, uint32_t n) -> String {
        if (at > size || n > size - at || n > 512) return "";
        std::string s;
        static const uint16_t cp1252[32] = {0x20ac, 0,      0x201a, 0x192,  0x201e, 0x2026, 0x2020, 0x2021,
                                            0x2c6,  0x2030, 0x160,  0x2039, 0x152,  0,      0x17d,  0,
                                            0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                            0x2dc,  0x2122, 0x161,  0x203a, 0x153,  0,      0x17e,  0x178};
        for (uint32_t i = 0; i < n; ++i) {
            unsigned c = p[at + i];
            if (encoding == 65001 || c < 128) {
                s += char(c);
                continue;
            }
            if (c < 160) c = cp1252[c - 128];
            if (!c) continue;
            if (c >= 2048) {
                s += char(0xe0 | (c >> 12));
                s += char(0x80 | ((c >> 6) & 63));
            } else
                s += char(0xc0 | (c >> 6));
            s += char(0x80 | (c & 63));
        }
        return clean(s);
    };
    const uint32_t title = be32(p + 84), length = be32(p + 88);
    r.title = text(title, length);
    const uint32_t exth = 16 + header;
    if ((be32(p + 128) & 0x40) && exth + 12 <= size && !memcmp(p + exth, "EXTH", 4)) {
        const uint32_t len = be32(p + exth + 4), count = be32(p + exth + 8);
        if (len < 12 || len > size - exth || count > 1024) return r;
        uint32_t at = exth + 12;
        for (uint32_t i = 0; i < count && at <= exth + len - 8; ++i) {
            const uint32_t type = be32(p + at), n = be32(p + at + 4);
            if (n < 8 || n > exth + len - at) break;
            if (type == 100 && !r.author.length()) r.author = text(at + 8, n - 8);
            if (type == 503) {
                const String t = text(at + 8, n - 8);
                if (t.length()) r.title = t;
            }
            at += n;
        }
    }
    return r;
}
}  // namespace
String fallbackTitle(const String& path) {
    String name = path.substring(path.lastIndexOf('/') + 1);
    const int dot = name.lastIndexOf('.');
    return dot > 0 ? name.substring(0, dot) : name;
}
Record extract(freeink::book::BookSource& source, const String& path, Cancelled cancel) {
    Record r;
    String lower = path;
    lower.toLowerCase();
    if (!(cancel && cancel()) && source.size() <= 64 * 1024 * 1024) {
        if (lower.endsWith(".fb2")) {
            Xml xml;
            xml.fb2 = true;
            if (xml.open()) {
                char buf[1024];
                uint32_t at = 0;
                bool ok = true;
                while (at < kXmlLimit && !xml.complete && ok) {
                    if (cancel && cancel()) {
                        ok = false;
                        break;
                    }
                    const int n = source.readAt(at, buf, sizeof(buf));
                    if (n < 0) {
                        ok = false;
                        break;
                    }
                    ok = xml.feed(buf, n, n == 0);
                    at += n;
                    if (!n) break;
                }
                if (ok && xml.complete) r = xml.result;
            }
        } else if (lower.endsWith(".epub")) {
            ZipEntry entry;
            Xml container;
            if (findEntry(source, "META-INF/container.xml", entry, cancel) &&
                xmlEntry(source, entry, container, cancel) && container.rootfile.length()) {
                Xml opf;
                if (findEntry(source, container.rootfile.c_str(), entry, cancel) &&
                    xmlEntry(source, entry, opf, cancel) && opf.complete)
                    r = opf.result;
            }
        } else if (lower.endsWith(".mobi") || lower.endsWith(".azw3") || lower.endsWith(".azw"))
            r = mobi(source, cancel);
    }
    if (!r.title.length()) r.title = fallbackTitle(path);
    return r;
}
}  // namespace OfflineMetadata
