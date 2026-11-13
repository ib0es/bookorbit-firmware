#include "opds_stream_parser.h"

#include <cstdlib>
#include <cstring>

namespace {

const char kEntryClose[] = "</entry>";
const size_t kEntryCloseLen = sizeof(kEntryClose) - 1;

bool isTagNameEnd(char c) { return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Finds "<name" followed by a tag-name terminator (so "<id" does not match "<identifier").
size_t findTag(const std::string& s, const char* name, size_t from, size_t end = std::string::npos) {
    const size_t nlen = strlen(name);
    if (end > s.size()) end = s.size();
    size_t pos = from;
    while ((pos = s.find(name, pos)) != std::string::npos) {
        if (pos + nlen >= end) return std::string::npos;
        if (isTagNameEnd(s[pos + nlen])) return pos;
        pos += nlen;
    }
    return std::string::npos;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && isspace((unsigned char)s[b])) b++;
    while (e > b && isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// Text content of the first <name ...>...</name> element, or "".
std::string elementText(const std::string& xml, const char* name) {
    size_t start = findTag(xml, (std::string("<") + name).c_str(), 0);
    if (start == std::string::npos) return "";
    size_t gt = xml.find('>', start);
    if (gt == std::string::npos) return "";
    if (xml[gt - 1] == '/') return "";   // <name/>
    std::string close = std::string("</") + name + ">";
    size_t end = xml.find(close, gt + 1);
    if (end == std::string::npos) return "";
    std::string text = xml.substr(gt + 1, end - gt - 1);
    // Unwrap CDATA if present.
    size_t cd = text.find("<![CDATA[");
    if (cd != std::string::npos) {
        size_t cdEnd = text.find("]]>", cd);
        if (cdEnd != std::string::npos) return trim(text.substr(cd + 9, cdEnd - cd - 9));
    }
    return trim(OpdsStreamParser::decodeEntities(text));
}

// Value of attribute `name` in a single tag like <link rel="x" href='y'>, or "".
std::string attribute(const std::string& tag, const char* name) {
    const size_t nlen = strlen(name);
    size_t pos = 0;
    while ((pos = tag.find(name, pos)) != std::string::npos) {
        bool boundaryBefore = pos > 0 && isspace((unsigned char)tag[pos - 1]);
        size_t p = pos + nlen;
        while (p < tag.size() && isspace((unsigned char)tag[p])) p++;
        if (boundaryBefore && p < tag.size() && tag[p] == '=') {
            p++;
            while (p < tag.size() && isspace((unsigned char)tag[p])) p++;
            if (p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) {
                char q = tag[p];
                size_t endq = tag.find(q, p + 1);
                if (endq == std::string::npos) return "";
                return OpdsStreamParser::decodeEntities(tag.substr(p + 1, endq - p - 1));
            }
        }
        pos += nlen;
    }
    return "";
}

int fileIdOf(const std::string& href) {
    const size_t fid = href.find("fileId=");
    return fid == std::string::npos ? 0 : atoi(href.c_str() + fid + 7);
}

}  // namespace

OpdsStreamParser::OpdsStreamParser(EntryCallback onEntry, size_t maxEntryBytes)
    : _onEntry(std::move(onEntry)), _maxEntryBytes(maxEntryBytes) {}

std::string OpdsStreamParser::decodeEntities(const std::string& in) {
    if (in.find('&') == std::string::npos) return in;
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] != '&') {
            out += in[i];
            continue;
        }
        size_t semi = in.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 10) {
            out += in[i];
            continue;
        }
        std::string ent = in.substr(i + 1, semi - i - 1);
        bool ok = true;
        if (ent == "amp")
            out += '&';
        else if (ent == "lt")
            out += '<';
        else if (ent == "gt")
            out += '>';
        else if (ent == "quot")
            out += '"';
        else if (ent == "apos")
            out += '\'';
        else if (ent.size() > 1 && ent[0] == '#') {
            char* endp = nullptr;
            unsigned long cp = (ent[1] == 'x' || ent[1] == 'X') ? strtoul(ent.c_str() + 2, &endp, 16)
                                                                : strtoul(ent.c_str() + 1, &endp, 10);
            if (endp && *endp == '\0' && cp > 0)
                appendUtf8(out, cp);
            else
                ok = false;
        } else {
            ok = false;
        }
        if (ok)
            i = semi;
        else
            out += in[i];
    }
    return out;
}

bool OpdsStreamParser::parseEntry(const std::string& xml, OpdsBookEntry& out) {
    out = OpdsBookEntry();
    out.title = elementText(xml, "title");
    out.author = elementText(xml, "name");   // <author><name>...</name></author>

    out.id = elementText(xml, "id");
    size_t colon = out.id.rfind(':');        // urn:bookorbit:book:123 -> 123
    if (colon != std::string::npos) out.id = out.id.substr(colon + 1);

    // Cover: the thumbnail is preferred. Book Orbit's full covers are print-size
    // progressive JPEGs (1500x2200, ~1 MB) that the device's decoder cannot
    // open; its thumbnails are baseline 400x587, enough for the home card and
    // the sleep screen. The full cover is used only when there is no thumbnail.
    size_t pos = 0;
    bool hasThumb = false;
    while ((pos = findTag(xml, "<link", pos)) != std::string::npos) {
        size_t gt = xml.find('>', pos);
        if (gt == std::string::npos) break;
        std::string tag = xml.substr(pos, gt - pos + 1);
        pos = gt + 1;

        std::string href = attribute(tag, "href");
        if (href.empty()) continue;

        const bool isThumb = tag.find("thumbnail") != std::string::npos;
        const bool isFullCover =
                !isThumb && (tag.find("cover") != std::string::npos || tag.find("image") != std::string::npos);
        if (isThumb) {
            out.coverUrl = href;
            hasThumb = true;
        } else if (isFullCover && !hasThumb) {
            out.coverUrl = href;
        }

        // One acquisition link per format (EPUB, MOBI, AZW3, FB2...), each its own file.
        const std::string linkRel = attribute(tag, "rel");
        const std::string type = attribute(tag, "type");
        const bool isAcquisition = linkRel.find("acquisition") != std::string::npos ||
                                   (linkRel.empty() && (type.find("application/epub") != std::string::npos ||
                                                        href.find("download") != std::string::npos));
        if (isAcquisition) {
            BookFormats::Ref ref;
            ref.kind = BookFormats::fromMime(type, attribute(tag, "title"), href);
            ref.fileId = fileIdOf(href);
            ref.url = href;
            bool dup = false;
            for (const auto& f : out.formats) dup = dup || (f.url == href);
            if (!dup) out.formats.push_back(ref);
        }

        // Series: only the link the server marks as such (rel ".../sort/series" or
        // a rel naming "series"), not any link whose address mentions a series.
        const std::string rel = attribute(tag, "rel");
        if (rel.find("series") != std::string::npos) {
            std::string stitle = attribute(tag, "title");
            if (!stitle.empty()) {
                size_t hash = stitle.rfind('#');
                if (hash != std::string::npos && hash + 1 < stitle.size()) {
                    out.seriesIndex = atoi(stitle.c_str() + hash + 1);
                    out.series = trim(stitle.substr(0, hash));
                } else {
                    out.series = trim(stitle);
                    out.seriesIndex = 0;
                }
            }
        }
    }

    if (out.title.empty() || out.formats.empty()) return false;

    // An address without "fileId=" (other OPDS servers): the book id stands in
    // for the only file, as before.
    for (auto& f : out.formats) {
        if (f.fileId <= 0 && out.formats.size() == 1 && !out.id.empty()) f.fileId = atoi(out.id.c_str());
    }
    const BookFormats::Ref& best = out.formats[BookFormats::preferred(out.formats)];
    out.downloadUrl = best.url;
    out.fileId = best.fileId;
    for (auto& f : out.formats) f.url = BookFormats::compactUrl(f.url, out.id, f.fileId);
    return true;
}

bool OpdsStreamParser::parseNavEntry(const std::string& xml, OpdsNavEntry& out) {
    out = OpdsNavEntry();
    out.title = elementText(xml, "title");
    out.content = elementText(xml, "content");
    out.id = elementText(xml, "id");

    size_t pos = 0;
    while ((pos = findTag(xml, "<link", pos)) != std::string::npos) {
        size_t gt = xml.find('>', pos);
        if (gt == std::string::npos) break;
        std::string tag = xml.substr(pos, gt - pos + 1);
        pos = gt + 1;

        std::string href = attribute(tag, "href");
        if (href.empty()) continue;

        std::string rel = attribute(tag, "rel");
        if (rel == "subsection" || tag.find("profile=opds-catalog") != std::string::npos || out.href.empty()) {
            out.href = href;
            if (rel == "subsection") break;
        }
    }

    return !out.title.empty() && !out.href.empty();
}

std::vector<OpdsNavEntry> OpdsStreamParser::parseNavigationFeed(const std::string& xml) {
    std::vector<OpdsNavEntry> entries;
    size_t pos = 0;
    while ((pos = findTag(xml, "<entry", pos)) != std::string::npos) {
        size_t end = xml.find("</entry>", pos);
        if (end == std::string::npos) break;
        std::string entryXml = xml.substr(pos, end + 8 - pos);
        pos = end + 8;

        OpdsNavEntry nav;
        if (parseNavEntry(entryXml, nav)) {
            entries.push_back(nav);
        }
    }
    return entries;
}

void OpdsStreamParser::scanFeedLevel(size_t end) {
    size_t pos = 0;
    while ((pos = findTag(_buf, "<link", pos, end)) != std::string::npos) {
        size_t gt = _buf.find('>', pos);
        if (gt == std::string::npos || gt >= end) break;
        std::string tag = _buf.substr(pos, gt - pos + 1);
        pos = gt + 1;
        if (attribute(tag, "rel") == "next") {
            _nextUrl = attribute(tag, "href");
        }
    }
}

bool OpdsStreamParser::feed(const char* data, size_t len) {
    if (_stopped) return false;
    _buf.append(data, len);

    while (true) {
        if (!_inEntry) {
            size_t start = findTag(_buf, "<entry", 0);
            if (start == std::string::npos) {
                // Keep only a possibly incomplete tag at the end of the buffer.
                size_t lt = _buf.rfind('<');
                size_t cut = (lt == std::string::npos) ? _buf.size() : lt;
                scanFeedLevel(cut);
                _buf.erase(0, cut);
                if (_buf.size() > _maxEntryBytes) _buf.clear();   // runaway tag
                return true;
            }
            scanFeedLevel(start);
            _buf.erase(0, start);
            _inEntry = true;
            _discardingEntry = false;
        }

        size_t end = _buf.find(kEntryClose);
        if (end == std::string::npos) {
            if (_buf.size() > _maxEntryBytes) {
                // Entry too large to hold: drop it, but keep enough tail to spot "</entry>".
                _discardingEntry = true;
                _buf.erase(0, _buf.size() - (kEntryCloseLen - 1));
            }
            return true;
        }

        bool accepted = false;
        if (!_discardingEntry) {
            OpdsBookEntry entry;
            if (parseEntry(_buf.substr(0, end), entry)) {
                accepted = true;
                _books++;
                if (_onEntry && !_onEntry(entry)) {
                    _stopped = true;
                }
            }
        }
        if (!accepted) _skipped++;

        _buf.erase(0, end + kEntryCloseLen);
        _inEntry = false;
        if (_stopped) {
            _buf.clear();
            return false;
        }
    }
}
