#include "reader/font_store.h"
#include "platform/font_partition.h"
#include "platform/hal/storage.h"
#include "core/font_catalog.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include <unistd.h>

void simPumpEvents() {}
namespace builtinfont {
const char* name() { return "Noto Serif"; }
}  // namespace builtinfont
namespace Platform {
void feedWatchdog() {}
}  // namespace Platform
static std::vector<uint8_t> memory(64 * 1024, 255);
static bool failCommit = false;
namespace FontPartition {
uint32_t capacity() { return memory.size(); }
bool read(uint32_t at, void* dst, uint32_t n) {
    if ((uint64_t)at + n > memory.size()) return false;
    memcpy(dst, memory.data() + at, n);
    return true;
}
bool eraseRange(uint32_t at, uint32_t n) {
    if (at % 4096 || n % 4096 || (uint64_t)at + n > memory.size()) return false;
    std::fill(memory.begin() + at, memory.begin() + at + n, 255);
    return true;
}
bool erase(uint32_t n, void (*)(void*, int), void*) { return eraseRange(0, FontCatalog::aligned(n)); }
bool write(uint32_t at, const void* src, uint32_t n) {
    if ((uint64_t)at + n > memory.size()) return false;
    const auto* p = (const uint8_t*)src;
    if (failCommit && at >= memory.size() - 8192) {
        memcpy(memory.data() + at, p, std::min(n, 8u));
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
        if ((memory[at + i] & p[i]) != p[i]) return false;
        memory[at + i] = p[i];
    }
    return true;
}
bool finish() { return true; }
const uint8_t* map(uint32_t n) { return n <= memory.size() ? memory.data() : nullptr; }
void unmap() {}
}  // namespace FontPartition
static FontStore::Family family(const std::filesystem::path& root, const char* name, size_t bytes, uint8_t value) {
    std::filesystem::create_directories(root / "fonts");
    const std::string file = std::string(name) + "-Regular.ttf";
    std::ofstream out(root / "fonts" / file, std::ios::binary);
    std::vector<char> data(bytes, value);
    out.write(data.data(), data.size());
    out.close();
    FontStore::Family f;
    f.name = name;
    f.files[0] = ("/fonts/" + file).c_str();
    f.bytes = bytes;
    return f;
}
static void checkFace(const char* name, uint8_t value) {
    assert(FontStore::map(name));
    uint32_t length = 0;
    const auto* bytes = FontStore::face(0, &length);
    assert(bytes && length && bytes[0] == value);
    FontStore::unmap();
}
int main() {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / ("font-catalog-test-" + std::to_string(getpid()));
    fs::create_directories(root);
    SDCardManager::getInstance().setRoot(root.string());
    assert(Storage.begin());
    auto a = family(root, "Alpha", 1000, 11), b = family(root, "Beta", 2000, 22), c = family(root, "Gamma", 3000, 33);
    assert(FontStore::installedFamilies().empty());
    auto builtin = family(root, "NotoSerif", 100, 99);
    const auto untouched = memory;
    assert(!FontStore::canInstall(builtin));
    assert(!FontStore::install(builtin, nullptr, nullptr));
    assert(!FontStore::removeInstalled("Noto Serif"));
    assert(memory == untouched);
    assert(FontStore::install(a, nullptr, nullptr));
    assert(FontStore::install(b, nullptr, nullptr));
    assert(FontStore::installedFamilies().size() == 2);
    checkFace("Alpha", 11);
    checkFace("Beta", 22);
    assert(FontStore::matchesInstalled(a));
    assert(FontStore::contains("beta"));
    const auto freeBefore = FontStore::freeBytes();
    assert(FontStore::removeInstalled("Alpha"));
    assert(!FontStore::contains("Alpha"));
    assert(FontStore::contains("Beta"));
    assert(FontStore::freeBytes() > freeBefore);
    checkFace("Beta", 22);
    assert(FontStore::install(c, nullptr, nullptr));
    checkFace("Gamma", 33);
    checkFace("Beta", 22);
    const auto previous = memory;
    auto huge = family(root, "Huge", 60 * 1024, 44);
    assert(!FontStore::install(huge, nullptr, nullptr));
    assert(memory == previous);
    failCommit = true;
    assert(!FontStore::removeInstalled("Beta"));
    failCommit = false;
    assert(FontStore::contains("Beta"));
    checkFace("Gamma", 33);
    auto d = family(root, "Delta", 1000, 55);
    failCommit = true;
    assert(!FontStore::install(d, nullptr, nullptr));
    failCommit = false;
    assert(!FontStore::contains("Delta"));
    assert(FontStore::installedFamilies().size() == 2);
    checkFace("Beta", 22);
    checkFace("Gamma", 33);
    assert(FontStore::install(d, nullptr, nullptr));
    checkFace("Delta", 55);
    // Replacement allocates a new block and leaves the other families intact.
    auto newB = family(root, "Beta", 2500, 66);
    assert(FontStore::install(newB, nullptr, nullptr));
    assert(FontStore::installedFamilies().size() == 3);
    checkFace("Beta", 66);
    checkFace("Gamma", 33);
    checkFace("Delta", 55);
    // Adopt the previous single-family FIBF format without rewriting its data.
    FontStore::unmap();
    std::fill(memory.begin(), memory.end(), 255);
    FontCatalog::Header legacy{};
    legacy.magic = 0x46424946;
    legacy.version = 1;
    strcpy(legacy.name, "Legacy");
    legacy.faces[0] = {4096, 1024};
    legacy.checksum = FontCatalog::checksum(&legacy, 104);
    memcpy(memory.data(), &legacy, sizeof(legacy));
    std::fill(memory.begin() + 4096, memory.begin() + 5120, 77);
    const std::vector<uint8_t> legacyData(memory.begin(), memory.begin() + 8192);
    assert(FontStore::contains("Legacy"));
    checkFace("Legacy", 77);
    assert(FontStore::install(a, nullptr, nullptr));
    assert(FontStore::installedFamilies().size() == 2);
    assert(std::equal(legacyData.begin(), legacyData.end(), memory.begin()));
    checkFace("Legacy", 77);
    checkFace("Alpha", 11);
    assert(FontStore::removeInstalled("Legacy"));
    assert(FontStore::contains("Alpha"));
    // Catalog entry count is enforced independently of the remaining byte capacity.
    FontStore::unmap();
    memory.assign(512 * 1024, 255);
    for (int i = 0; i < 32; i++) {
        const auto name = std::string("Family") + std::to_string(i);
        auto f = family(root, name.c_str(), 100, (uint8_t)i);
        assert(FontStore::install(f, nullptr, nullptr));
    }
    assert(FontStore::installedFamilies().size() == 32);
    const auto fullCatalog = memory;
    auto extra = family(root, "ThirtyThird", 100, 123);
    assert(!FontStore::install(extra, nullptr, nullptr));
    assert(memory == fullCatalog);
    // SD removal deletes every face and duplicate, retaining Flash and other families.
    const auto flashBeforeCardDelete = memory;
    fs::create_directories(root / "fonts" / "Family0");
    fs::copy_file(root / "fonts" / "Family0-Regular.ttf", root / "fonts" / "Family0" / "Family0-Bold.ttf");
    fs::copy_file(root / "fonts" / "Family0-Regular.ttf", root / "fonts" / "Family0" / "Family0-Regular.ttf");
    assert(FontStore::removeCard("family0"));
    assert(!fs::exists(root / "fonts" / "Family0-Regular.ttf"));
    assert(!fs::exists(root / "fonts" / "Family0" / "Family0-Bold.ttf"));
    assert(!fs::exists(root / "fonts" / "Family0" / "Family0-Regular.ttf"));
    assert(fs::exists(root / "fonts" / "Family1-Regular.ttf"));
    assert(FontStore::contains("Family0"));
    assert(memory == flashBeforeCardDelete);
    assert(!FontStore::removeCard("Family0"));
    assert(!FontStore::removeCard("../Family1"));
    assert(!FontStore::removeCard("Noto Serif"));
    assert(fs::exists(root / "fonts" / "NotoSerif-Regular.ttf"));
    assert(FontStore::removeInstalled("Family1"));
    assert(fs::exists(root / "fonts" / "Family1-Regular.ttf"));
    fs::remove_all(root);
    puts("FontStore multi-family, legacy migration, bounds and interrupted-commit checks passed.");
}
