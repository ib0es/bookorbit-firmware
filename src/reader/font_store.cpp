#include "font_store.h"
#include "fonts/builtin_font.h"

#include <string.h>

#include <algorithm>

#include "../platform/hal/storage.h"
#include "../core/font_catalog.h"
#include "../platform/system.h"
#include "../platform/font_partition.h"

namespace FontStore {

namespace {

using FontCatalog::Header;
using FontCatalog::Directory;
using FontCatalog::Record;
constexpr uint32_t kHeaderBytes = FontCatalog::kSector;
struct State {
    Directory directory{};
    int slot = -1;
    bool ok = true;
};
uint32_t dataLimit() {
    const auto cap = FontPartition::capacity();
    return cap > FontCatalog::kReserved ? cap - FontCatalog::kReserved : 0;
}
bool readHeader(uint32_t offset, uint32_t span, Header& h) {
    return FontPartition::read(offset, &h, sizeof(h)) && FontCatalog::valid(h, span);
}
State loadState() {
    State state;
    const uint32_t limit = dataLimit();
    if (!limit) {
        state.ok = false;
        return state;
    }
    for (int slot = 0; slot < 2; slot++) {
        Directory candidate{};
        if (!FontPartition::read(limit + slot * kHeaderBytes, &candidate, sizeof(candidate))) {
            state.ok = false;
            return state;
        }
        if (!FontCatalog::valid(candidate, limit)) continue;
        if (state.slot < 0 || FontCatalog::newer(candidate.generation, state.directory.generation)) {
            state.directory = candidate;
            state.slot = slot;
        }
    }
    if (state.slot < 0) {
        Header legacy{};
        if (!FontPartition::read(0, &legacy, sizeof(legacy))) {
            state.ok = false;
            return state;
        }
        if (FontCatalog::valid(legacy, FontPartition::capacity())) {
            const uint32_t span = FontCatalog::span(legacy);
            state.directory.records[0] = {0, span};
            state.directory.count = 1;
        }
    }
    return state;
}
bool commit(State& state) {
    if (!state.ok) return false;
    auto& d = state.directory;
    d.magic = FontCatalog::kMagic;
    d.version = 2;
    d.generation++;
    d.checksum = FontCatalog::checksum(&d, offsetof(Directory, checksum));
    const int slot = state.slot == 0 ? 1 : 0;
    const uint32_t offset = dataLimit() + slot * kHeaderBytes;
    if (!FontCatalog::valid(d, dataLimit()) || !FontPartition::eraseRange(offset, kHeaderBytes) ||
        !FontPartition::write(offset, &d, sizeof(d)) || !FontPartition::finish())
        return false;
    Directory verified{};
    return FontPartition::read(offset, &verified, sizeof(verified)) && memcmp(&verified, &d, sizeof(d)) == 0;
}
int findRecord(const State& state, const String& name, Header* header = nullptr) {
    for (uint32_t i = 0; i < state.directory.count; i++) {
        const auto& r = state.directory.records[i];
        Header h{};
        if (readHeader(r.offset, r.span, h) && sameFamilyName(h.name, name)) {
            if (header) *header = h;
            return (int)i;
        }
    }
    return -1;
}

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

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
    struct {
        const char* s;
        int style;
    } k[] = {{"regular", 0}, {"book", 0},       {"roman", 0},       {"normal", 0}, {"bold", 1}, {"italic", 2},
             {"oblique", 2}, {"bolditalic", 3}, {"boldoblique", 3}, {"bi", 3},     {"it", 2}};

    if (dash <= 0) {
        String lower = stem;
        lower.toLowerCase();
        lower.replace(" ", "");
        for (const auto& e : k) {
            if (lower == e.s) {
                *nameStyle = e.style;
                return "";
            }
        }
        return stem;
    }
    String suffix = stem.substring(dash + 1);
    suffix.toLowerCase();
    suffix.replace(" ", "");
    for (const auto& e : k)
        if (suffix == e.s) {
            *nameStyle = e.style;
            return stem.substring(0, dash);
        }
    return stem;
}

const uint8_t* s_base = nullptr;

Header s_header{};
bool s_mapped = false;

}  // namespace

uint32_t capacity() {
    const uint32_t size = FontPartition::capacity();
    return size > FontCatalog::kReserved ? size - FontCatalog::kReserved : 0;
}

String familyNameForFile(const String& file) {
    const int dot = file.lastIndexOf('.');
    int style;
    return familyOf(dot > 0 ? file.substring(0, dot) : file, &style);
}

bool sameFamilyName(const String& left, const String& right) {
    auto key = [](const String& name) {
        String result;
        for (size_t i = 0; i < name.length(); i++) {
            const char c = name[i];
            if (c != ' ' && c != '-' && c != '_') result += c;
        }
        result.toLowerCase();
        return result;
    };
    return left.length() && right.length() && key(left) == key(right);
}

bool removeInstalled(const String& name) {
    if (sameFamilyName(name, builtinfont::name())) return false;
    State state = loadState();
    const int index = findRecord(state, name);
    if (!state.ok || index < 0) return false;
    unmap();
    for (uint32_t i = index + 1; i < state.directory.count; i++)
        state.directory.records[i - 1] = state.directory.records[i];
    state.directory.records[--state.directory.count] = {};
    return commit(state);
}

std::vector<Family> installedFamilies() {
    const State state = loadState();
    std::vector<Family> families;
    if (!state.ok) return families;
    for (uint32_t i = 0; i < state.directory.count; i++) {
        const auto& r = state.directory.records[i];
        Header h{};
        if (!readHeader(r.offset, r.span, h)) continue;
        Family f;
        f.name = h.name;
        for (const auto& face : h.faces) f.bytes += face.length;
        families.push_back(f);
    }
    return families;
}
bool contains(const String& name) {
    const State state = loadState();
    return state.ok && findRecord(state, name) >= 0;
}
uint32_t freeBytes() {
    const State state = loadState();
    if (!state.ok) return 0;
    uint64_t used = 0;
    for (uint32_t i = 0; i < state.directory.count; i++) used += state.directory.records[i].span;
    return used <= capacity() ? capacity() - (uint32_t)used : 0;
}
uint32_t largestFreeBytes() {
    const State state = loadState();
    return state.ok ? FontCatalog::largestFree(state.directory, capacity()) : 0;
}
bool canInstall(const Family& family) {
    if (sameFamilyName(family.name, builtinfont::name())) return false;
    const State state = loadState();
    if (!state.ok || !family.files[0].length() ||
        (state.directory.count >= FontCatalog::kMax && findRecord(state, family.name) < 0))
        return false;
    uint64_t bytes = kHeaderBytes;
    for (const auto& path : family.files) {
        if (!path.length()) continue;
        FsFile file = Storage.open(path.c_str(), O_RDONLY);
        if (!file) return false;
        bytes += (file.size() + uint64_t(3)) & ~uint64_t(3);
        file.close();
    }
    return bytes <= capacity() &&
           FontCatalog::findSpace(state.directory, capacity(), FontCatalog::aligned((uint32_t)bytes)) != UINT32_MAX;
}

bool removeCard(const String& name) {
    if (!name.length() || sameFamilyName(name, builtinfont::name()) || !Storage.isAvailable()) return false;
    std::vector<String> dirs = {String(kDir)};
    for (const String& sub : Storage.listSubdirs(kDir)) {
        if (!sub.startsWith(".")) dirs.push_back(String(kDir) + "/" + sub);
    }
    bool removed = false;
    for (const String& dir : dirs) {
        for (const String& file : Storage.listFileNames(dir)) {
            String lower = file;
            lower.toLowerCase();
            if (file.startsWith(".") || !(lower.endsWith(".ttf") || lower.endsWith(".otf")) ||
                !sameFamilyName(familyNameForFile(file), name))
                continue;
            if (!Storage.remove((dir + "/" + file).c_str())) return false;
            removed = true;
            Platform::feedWatchdog();
        }
    }
    return removed;
}

std::vector<Family> scanCard() {
    std::vector<Family> out;
    if (!Storage.exists(kDir)) return out;

    std::vector<String> dirs = {String(kDir)};
    for (const String& sub : Storage.listSubdirs(kDir)) {
        if (!sub.startsWith(".")) {
            dirs.push_back(String(kDir) + "/" + sub);
        }
    }

    for (const String& dir : dirs) {
        for (const String& n : Storage.listFileNames(dir)) {
            String lower = n;
            lower.toLowerCase();
            if (n.startsWith(".") || !(lower.endsWith(".ttf") || lower.endsWith(".otf"))) continue;
            const String path = dir + "/" + n;
            int nameStyle;
            String fam = familyOf(n.substring(0, n.length() - 4), &nameStyle);
            if (fam.length() == 0 && dir != kDir) {
                const int slash = dir.lastIndexOf('/');
                if (slash >= 0) fam = dir.substring(slash + 1);
            }
            if (fam.length() == 0 || sameFamilyName(fam, builtinfont::name())) continue;
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
    const auto families = installedFamilies();
    return families.empty() ? String() : families.front().name;
}

bool matchesInstalled(const Family& family) {
    Header header;
    const State state = loadState();
    const int index = findRecord(state, family.name, &header);
    if (!state.ok || index < 0) return false;
    const uint32_t base = state.directory.records[index].offset;
    uint8_t card[512], flash[512];
    for (int style = 0; style < 4; style++) {
        const auto& face = header.faces[style];
        if (!family.files[style].length()) {
            if (face.length) return false;
            continue;
        }
        FsFile file = Storage.open(family.files[style].c_str(), O_RDONLY);
        if (!file) return false;
        bool equal = file.size() == face.length;
        for (uint32_t at = 0; equal && at < face.length;) {
            const uint32_t count = std::min<uint32_t>(sizeof(card), face.length - at);
            equal = file.read(card, count) == (int)count &&
                    FontPartition::read(base + face.offset + at, flash, count) && memcmp(card, flash, count) == 0;
            at += count;
        }
        file.close();
        if (!equal) return false;
    }
    return true;
}

bool install(const Family& family, ProgressFn progress, void* user) {
    if (sameFamilyName(family.name, builtinfont::name())) return false;
    unmap();
    if (family.name.length() == 0 || family.name.length() >= 64 || !family.files[0].length()) {
        Serial.printf("[Fonts] %s: %u bytes do not fit the font partition (%u)\n", family.name.c_str(),
                      (unsigned)family.bytes, (unsigned)capacity());
        return false;
    }
    const uint32_t t0 = millis();
    Header h{};
    h.magic = 0x46424946u;
    h.version = 1;
    strncpy(h.name, family.name.c_str(), sizeof(h.name) - 1);
    uint32_t at = kHeaderBytes;
    for (int s = 0; s < 4; s++) {
        if (!family.files[s].length()) continue;
        FsFile f = Storage.open(family.files[s].c_str(), O_RDONLY);
        if (!f) return false;
        const uint64_t length = f.size();
        f.close();
        const uint64_t end = (uint64_t)at + ((length + 3) & ~uint64_t(3));
        if (length > capacity() || end > (uint64_t)capacity() + kHeaderBytes) return false;
        h.faces[s].offset = at;
        h.faces[s].length = (uint32_t)length;
        at = (uint32_t)end;
    }
    h.checksum = FontCatalog::checksum(&h, offsetof(Header, checksum));
    const uint32_t total = at;
    if (total > capacity() + kHeaderBytes) return false;

    State state = loadState();
    if (!state.ok) return false;
    const int previous = findRecord(state, family.name);
    if (previous < 0 && state.directory.count >= FontCatalog::kMax) return false;
    const uint32_t span = FontCatalog::aligned(total);
    const uint32_t base = FontCatalog::findSpace(state.directory, dataLimit(), span);
    if (base == UINT32_MAX) return false;
    // Write only unused sectors. The previous catalog remains valid until commit.
    for (uint32_t at = 0; at < span; at += kHeaderBytes) {
        if (!FontPartition::eraseRange(base + at, kHeaderBytes)) return false;
        Platform::feedWatchdog();
    }
    uint8_t buf[2048];
    uint32_t done = 0;
    for (int s = 0; s < 4; s++) {
        if (!h.faces[s].length) continue;
        FsFile f = Storage.open(family.files[s].c_str(), O_RDONLY);
        if (!f) return false;
        uint32_t off = base + h.faces[s].offset;
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
    if (!FontPartition::write(base, &h, sizeof(h)) || !FontPartition::finish()) return false;
    const uint32_t index = previous < 0 ? state.directory.count++ : (uint32_t)previous;
    state.directory.records[index] = {base, span};
    if (!commit(state)) return false;
    Serial.printf("[Fonts] %s installed: %u KB in %lu ms\n", family.name.c_str(), (unsigned)(total / 1024),
                  (unsigned long)(millis() - t0));
    if (progress) progress(user, 1000);
    return true;
}

bool map(const String& name) {
    if (s_mapped) unmap();
    const State state = loadState();
    Header h{};
    const int index = findRecord(state, name, &h);
    if (!state.ok || index < 0) return false;
    const auto& record = state.directory.records[index];
    s_base = FontPartition::map(record.offset + record.span);
    if (!s_base) return false;
    s_base += record.offset;
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
