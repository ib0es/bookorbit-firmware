#include "font_store.h"

#include <string.h>

#include <algorithm>

#include "../platform/hal/storage.h"
#include "../platform/font_partition.h"

namespace FontStore {

namespace {

// The partition: a 4 KB header, then the faces (each 4-byte aligned).
constexpr uint32_t kHeaderBytes = 4096;
constexpr uint32_t kMagic = 0x46424946u;   // "FIBF"
constexpr uint32_t kVersion = 1;

struct Header {
    uint32_t magic;
    uint32_t version;
    char name[64];
    struct { uint32_t offset, length; } faces[4];
    uint32_t checksum;   // of the fields above
};

uint32_t checksumOf(const Header& h) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&h);
    uint32_t s = 2166136261u;
    for (size_t i = 0; i < offsetof(Header, checksum); i++) s = (s ^ p[i]) * 16777619u;
    return s;
}

bool valid(const Header& h, uint32_t cap) {
    if (h.magic != kMagic || h.version != kVersion || h.checksum != checksumOf(h)) return false;
    if (memchr(h.name, 0, sizeof(h.name)) == nullptr) return false;
    for (const auto& f : h.faces)
        if (f.length && (f.offset < kHeaderBytes || f.offset + f.length > cap + kHeaderBytes)) return false;
    return h.faces[0].length > 0;
}

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

// Style of a font file from its 'head' table (macStyle: bit 0 bold, bit 1
// italic); -1 when it is not a TrueType/OpenType font.
int styleOf(const String& path) {
    FsFile f = Storage.open(path.c_str(), O_RDONLY);
    if (!f) return -1;
    uint8_t dir[12 + 16 * 40];
    const int n = f.read(dir, sizeof(dir));
    int style = -1;
    if (n >= 12) {
        const uint32_t tag = be32(dir);
        if (tag == 0x00010000u || tag == 0x4F54544Fu /* OTTO */ || tag == 0x74727565u /* true */) {
            const uint16_t tables = be16(dir + 4);
            for (uint16_t t = 0; t < tables && 12 + 16 * (t + 1) <= (uint32_t)n; t++) {
                const uint8_t* e = dir + 12 + 16 * t;
                if (memcmp(e, "head", 4) != 0) continue;
                uint8_t head[54];
                if (f.seek(be32(e + 8)) && f.read(head, sizeof(head)) == (int)sizeof(head)) style = be16(head + 44) & 3;
                break;
            }
        }
    }
    f.close();
    return style;
}

// "Literata-BoldItalic" -> family "Literata", style from the name (-1 unknown).
String familyOf(const String& stem, int* nameStyle) {
    *nameStyle = -1;
    const int dash = stem.lastIndexOf('-');
    if (dash <= 0) return stem;
    String suffix = stem.substring(dash + 1);
    suffix.toLowerCase();
    suffix.replace(" ", "");
    struct { const char* s; int style; } k[] = {
        {"regular", 0}, {"book", 0}, {"roman", 0}, {"normal", 0}, {"bold", 1}, {"italic", 2}, {"oblique", 2},
        {"bolditalic", 3}, {"boldoblique", 3}, {"bi", 3}, {"it", 2}};
    for (const auto& e : k)
        if (suffix == e.s) {
            *nameStyle = e.style;
            return stem.substring(0, dash);
        }
    return stem;
}

const uint8_t* s_base = nullptr;

bool readHeader(Header& h) { return FontPartition::read(0, &h, sizeof(h)) && valid(h, capacity()); }

Header s_header{};
bool s_mapped = false;

}  // namespace

uint32_t capacity() {
    const uint32_t size = FontPartition::capacity();
    return size > kHeaderBytes ? size - kHeaderBytes : 0;
}

std::vector<Family> scanCard() {
    std::vector<Family> out;
    if (!Storage.exists(kDir)) return out;
    for (const String& n : Storage.listFileNames(kDir)) {
        String lower = n;
        lower.toLowerCase();
        if (n.startsWith(".") || !(lower.endsWith(".ttf") || lower.endsWith(".otf"))) continue;
        const String path = String(kDir) + "/" + n;
        int nameStyle;
        const String fam = familyOf(n.substring(0, n.length() - 4), &nameStyle);
        int style = styleOf(path);
        if (style < 0) continue;   // not a font
        if (nameStyle >= 0 && style == 0) style = nameStyle;   // some fonts leave macStyle at 0
        auto it = std::find_if(out.begin(), out.end(), [&](const Family& f) { return f.name == fam; });
        if (it == out.end()) {
            out.push_back(Family());
            it = out.end() - 1;
            it->name = fam;
        }
        if (it->files[style].length()) continue;   // two files for one style: the first
        it->files[style] = path;
        FsFile f = Storage.open(path.c_str(), O_RDONLY);
        if (f) {
            it->bytes += (uint32_t)f.size();
            f.close();
        }
    }
    // A family needs its regular face (or one face, taken as regular).
    for (Family& f : out) {
        if (f.files[0].length()) continue;
        for (int s = 1; s < 4; s++)
            if (f.files[s].length()) {
                f.files[0] = f.files[s];
                f.files[s] = "";
                break;
            }
    }
    std::sort(out.begin(), out.end(), [](const Family& a, const Family& b) { return a.name < b.name; });
    return out;
}

String installedName() {
    Header h;
    return readHeader(h) ? String(h.name) : String();
}

bool install(const Family& family, ProgressFn progress, void* user) {
    unmap();
    if (family.bytes + 16 > capacity() || !family.files[0].length()) {
        Serial.printf("[Fonts] %s: %u bytes do not fit the font partition (%u)\n", family.name.c_str(),
                      (unsigned)family.bytes, (unsigned)capacity());
        return false;
    }
    const uint32_t t0 = millis();
    Header h{};
    h.magic = kMagic;
    h.version = kVersion;
    strncpy(h.name, family.name.c_str(), sizeof(h.name) - 1);
    uint32_t at = kHeaderBytes;
    for (int s = 0; s < 4; s++) {
        if (!family.files[s].length()) continue;
        FsFile f = Storage.open(family.files[s].c_str(), O_RDONLY);
        if (!f) return false;
        h.faces[s].offset = at;
        h.faces[s].length = (uint32_t)f.size();
        f.close();
        at += (h.faces[s].length + 3) & ~3u;
    }
    h.checksum = checksumOf(h);
    const uint32_t total = at;

    // The header goes first (erased: no family), then the faces, then the header.
    if (!FontPartition::erase(total, progress, user)) return false;
    uint8_t buf[2048];
    uint32_t done = 0;
    for (int s = 0; s < 4; s++) {
        if (!h.faces[s].length) continue;
        FsFile f = Storage.open(family.files[s].c_str(), O_RDONLY);
        if (!f) return false;
        uint32_t off = h.faces[s].offset;
        for (uint32_t left = h.faces[s].length; left > 0;) {
            const int n = f.read(buf, std::min<uint32_t>(sizeof(buf), left));
            if (n <= 0) {
                f.close();
                return false;
            }
            uint32_t w = (uint32_t)n;
            while (w & 3) buf[w++] = 0;   // writes in whole words
            if (!FontPartition::write(off, buf, w)) {
                f.close();
                return false;
            }
            off += w;
            left -= (uint32_t)n;
            done += (uint32_t)n;
            if (progress) progress(user, 300 + (int)(690ull * done / (family.bytes ? family.bytes : 1)));
        }
        f.close();
    }
    if (!FontPartition::write(0, &h, sizeof(h))) return false;
    FontPartition::finish();
    Serial.printf("[Fonts] %s installed: %u KB in %lu ms\n", family.name.c_str(), (unsigned)(total / 1024),
                  (unsigned long)(millis() - t0));
    if (progress) progress(user, 1000);
    return true;
}

bool map() {
    if (s_mapped) return true;
    Header h;
    if (!readHeader(h)) return false;
    uint32_t end = 0;
    for (const auto& f : h.faces) end = std::max(end, f.offset + f.length);
    s_base = FontPartition::map(end);
    if (!s_base) return false;
    s_header = h;
    s_mapped = true;
    return true;
}

void unmap() {
    if (!s_mapped) return;
    FontPartition::unmap();
    s_base = nullptr;
    s_mapped = false;
}

const uint8_t* face(int style, uint32_t* len) {
    if (!s_mapped || style < 0 || style > 3 || !s_header.faces[style].length) return nullptr;
    *len = s_header.faces[style].length;
    return s_base + s_header.faces[style].offset;
}

}  // namespace FontStore
