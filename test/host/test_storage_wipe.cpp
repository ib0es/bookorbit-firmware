// Host unit tests for StorageManager::wipeAll().
// Run: test/host/run.sh
#include "platform/hal/storage.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

void simPumpEvents() {}

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void writeDummy(const fs::path& p, const char* content = "data") {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << content;
}

int main() {
    fs::path tmp = fs::temp_directory_path() / ("bookorbit_wipe_test_" + std::to_string(getpid()));
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    SDCardManager::getInstance().setRoot(tmp.string());

    writeDummy(tmp / "config.json", "{\"wifi_ssid\":\"test\"}");
    writeDummy(tmp / ".hidden_file", "x");
    writeDummy(tmp / ".crosspoint" / "state.bin", "cp");
    writeDummy(tmp / "books" / "nested" / "Книга на русском.epub", "epub");
    writeDummy(tmp / "offline" / "Local.fb2", "fb2");
    writeDummy(tmp / "covers" / "cover.jpg", "jpg");
    writeDummy(tmp / "cache" / "sub" / "page.bin", "cache");
    writeDummy(tmp / "factory" / "a" / "b" / "c.txt", "oem");

    const bool ok = Storage.wipeAll();
    CHECK(ok);
    CHECK(fs::exists(tmp));
    CHECK(!fs::exists(tmp / "config.json"));
    CHECK(!fs::exists(tmp / ".hidden_file"));
    CHECK(!fs::exists(tmp / ".crosspoint"));
    CHECK(!fs::exists(tmp / "factory"));

    CHECK(fs::exists(tmp / "books") && fs::is_directory(tmp / "books"));
    CHECK(fs::exists(tmp / "offline") && fs::is_directory(tmp / "offline"));
    CHECK(fs::exists(tmp / "covers") && fs::is_directory(tmp / "covers"));
    CHECK(fs::exists(tmp / "cache") && fs::is_directory(tmp / "cache"));
    CHECK(fs::is_empty(tmp / "books"));
    CHECK(fs::is_empty(tmp / "offline"));
    CHECK(fs::is_empty(tmp / "covers"));
    CHECK(fs::is_empty(tmp / "cache"));

    // Calling wipeAll when card is unavailable returns false without touching anything.
    SDCardManager::getInstance().shutdown();
    CHECK(!Storage.wipeAll());

    fs::remove_all(tmp);
    printf("OK: %d/%d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
