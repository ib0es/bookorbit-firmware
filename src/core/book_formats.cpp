#include "book_formats.h"

#include <algorithm>
#include <cctype>

namespace BookFormats {

namespace {

std::string lower(const std::string& s) {
    std::string out(s);
    for (char& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

bool endsWith(const std::string& s, const char* sfx) {
    const size_t n = std::char_traits<char>::length(sfx);
    return s.size() >= n && s.compare(s.size() - n, n, sfx) == 0;
}

struct Info {
    Kind kind;
    const char* ext;
    const char* label;
    int rank;
    bool readable;
};
// Rank: EPUB is what the reader handles best; AZW3 (KF8) keeps the most of the
// book's formatting of the converted ones; old MOBI / AZW last.
const Info kInfo[] = {
        {Unknown, "", "?", 99, false},    {Epub, ".epub", "EPUB", 0, true},
        {Fb2, ".fb2", "FB2", 2, true},   // converted to EPUB on first open
        {Mobi, ".mobi", "MOBI", 3, true},   // converted to EPUB on first open
        {Azw3, ".azw3", "AZW3", 1, true},   // KF8: converted to EPUB on first open
        {Azw, ".azw", "AZW", 4, true},   // old Kindle: MOBI 6 (refused if DRM-protected)
        {Txt, ".txt", "TXT", 5, true},
};

const Info& info(Kind k) { return kInfo[k < KindCount ? k : Unknown]; }

Kind fromLabel(const std::string& t) {
    const std::string l = lower(t);
    for (const Info& i : kInfo) {
        if (i.kind != Unknown && l == lower(i.label)) return i.kind;
    }
    return Unknown;
}

}  // namespace

Kind fromMime(const std::string& mimeIn, const std::string& title, const std::string& href) {
    std::string mime = lower(mimeIn);
    const size_t semi = mime.find(';');
    if (semi != std::string::npos) mime.erase(semi);
    while (!mime.empty() && mime.back() == ' ') mime.pop_back();
    if (mime == "application/epub+zip") return Epub;
    if (mime == "application/x-fictionbook+xml" || mime == "application/x-fictionbook" || mime == "text/fb2+xml")
        return Fb2;
    if (mime == "application/x-mobipocket-ebook" || mime == "application/x-mobi8-ebook") return Mobi;
    if (mime == "application/vnd.amazon.ebook" || mime == "application/x-mobi8") {
        // Book Orbit marks both AZW and AZW3 with the Amazon type: the title says which.
        const Kind byTitle = fromLabel(title);
        return byTitle == Azw ? Azw : Azw3;
    }
    if (mime == "text/plain") return Txt;
    const Kind byTitle = fromLabel(title);
    if (byTitle != Unknown) return byTitle;
    if (!href.empty()) {
        std::string path = href.substr(0, href.find('?'));
        return fromPath(path);
    }
    return Unknown;
}

Kind fromPath(const std::string& path) {
    const std::string l = lower(path);
    for (const Info& i : kInfo) {
        if (i.kind != Unknown && endsWith(l, i.ext)) return i.kind;
    }
    return Unknown;
}

const char* extension(Kind k) { return info(k).ext; }
const char* label(Kind k) { return info(k).label; }
bool readable(Kind k) { return info(k).readable; }
int rank(Kind k) { return info(k).rank; }

std::string downloadPath(const std::string& bookId, int fileId) {
    return "/api/v1/opds/" + bookId + "/download?fileId=" + std::to_string(fileId);
}

std::string urlFor(const Ref& r, const std::string& bookId) {
    if (!r.url.empty()) return r.url;
    if (bookId.empty() || r.fileId <= 0) return "";
    return downloadPath(bookId, r.fileId);
}

std::string compactUrl(const std::string& url, const std::string& bookId, int fileId) {
    // Only a numeric book id: the library rebuilds the address from BookItem::bookId.
    const bool numeric = !bookId.empty() && bookId.find_first_not_of("0123456789") == std::string::npos;
    if (numeric && fileId > 0) {
        const std::string canon = downloadPath(bookId, fileId);
        if (url == canon) return "";
        // An absolute address of the same path is canonical too (the host is the server's).
        if (endsWith(url, canon.c_str()) && url.compare(0, 4, "http") == 0) {
            const size_t scheme = url.find("://");
            if (scheme != std::string::npos && url.find('/', scheme + 3) == url.size() - canon.size()) return "";
        }
    }
    return url;
}

int preferred(const std::vector<Ref>& refs) {
    int best = -1;
    for (int i = 0; i < (int)refs.size(); i++) {
        if (best < 0) {
            best = i;
            continue;
        }
        const Ref& a = refs[i];
        const Ref& b = refs[best];
        if (readable(a.kind) != readable(b.kind)) {
            if (readable(a.kind)) best = i;
            continue;
        }
        if (rank(a.kind) < rank(b.kind)) best = i;
    }
    return best;
}

std::string labels(const std::vector<Kind>& kindsIn) {
    std::vector<Kind> kinds(kindsIn);
    std::sort(kinds.begin(), kinds.end(), [](Kind a, Kind b) { return rank(a) < rank(b); });
    kinds.erase(std::unique(kinds.begin(), kinds.end()), kinds.end());
    std::string out;
    for (Kind k : kinds) {
        if (!out.empty()) out += " \xC2\xB7 ";   // " · "
        out += label(k);
    }
    return out;
}

}  // namespace BookFormats
