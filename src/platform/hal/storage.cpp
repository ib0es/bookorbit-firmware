#include "storage.h"
#include <algorithm>
#include <cstring>
#ifdef ESP_PLATFORM
#include <esp_task_wdt.h>
#endif
#include "../../core/book_formats.h"
#include "../device.h"

StorageManager Storage;

StorageManager::StorageManager() {}

bool StorageManager::begin() {
    Serial.println("[Storage] Initializing FreeInk SDCardManager...");

    // SD rail power. Profiles that declare it (X3: GPIO13 active-high; X4 Classic
    // and X4 Pro: active-low SDMMC enables) are left to SDCardManager, which
    // knows the polarity and the power cycle. The device profile names the
    // switch an SDK profile leaves unassigned (the C3 X4's GPIO13, active-high).
    if (BoardConfig::ACTIVE.sd.powerEnable < 0 && Platform::kDevice.sdPowerPin >= 0) {
        const int8_t sdPower = Platform::kDevice.sdPowerPin;
        gpio_hold_dis((gpio_num_t)sdPower);
        pinMode(sdPower, OUTPUT);
        digitalWrite(sdPower, HIGH);
        delay(20);
        BoardConfig::ACTIVE.sd.powerEnable = sdPower;
        BoardConfig::ACTIVE.sd.powerActiveHigh = true;
    }

    SDCardManager& sd = SDCardManager::getInstance();

    bool ok = false;
#if defined(FREEINK_SD_SDMMC) && FREEINK_SD_SDMMC
    // Native SDMMC mounts at bus width/clock internally; SPI frequency probing
    // is skipped.
    ok = sd.begin();
#else
    // Probe at 40 MHz (fastest for X4), then 20 MHz, then 10 MHz
    for (uint32_t freq : {40000000u, 20000000u, 10000000u}) {
        BoardConfig::ACTIVE.sd.spiHz = freq;
        if (sd.begin()) {
            ok = true;
            break;
        }
        delay(20);
    }
#endif

    if (!ok) {
        Serial.println("[Storage] Warning: MicroSD mount failed via SDCardManager!");
        return false;
    }

    Serial.printf("[Storage] MicroSD mounted successfully. Size: %llu MB\n", totalBytes() / (1024 * 1024));
    ensureDirectories();
    return true;
}

void StorageManager::shutdown() { SDCardManager::getInstance().shutdown(); }

FsBlockDeviceInterface* StorageManager::detachForUsbMsc() {
    return SDCardManager::getInstance().detachFilesystemForRawAccess();
}

bool StorageManager::isAvailable() const { return SDCardManager::getInstance().ready(); }

uint64_t StorageManager::totalBytes() const { return SDCardManager::getInstance().sdTotalBytes(); }

uint64_t StorageManager::usedBytes() {
    // SDCardManager counts the free clusters by walking the whole FAT: seconds on
    // a big card, in one call that cannot feed the task watchdog. This task is
    // taken off the watchdog for the count, and the answer kept for a minute.
    static uint64_t s_used = 0;
    static uint32_t s_at = 0;
    static bool s_valid = false;
    const uint32_t now = millis();
    if (s_valid && now - s_at < 60000) return s_used;
#ifdef ESP_PLATFORM
    const bool watched = esp_task_wdt_status(nullptr) == ESP_OK;
    if (watched) {
        esp_task_wdt_reset();
        esp_task_wdt_delete(nullptr);
    }
#endif
    s_used = SDCardManager::getInstance().sdUsedBytes();
#ifdef ESP_PLATFORM
    if (watched) esp_task_wdt_add(nullptr);
#endif
    s_at = millis();
    s_valid = true;
    return s_used;
}

uint64_t StorageManager::freeBytes() {
    uint64_t total = totalBytes();
    uint64_t used = usedBytes();
    return (total > used) ? (total - used) : 0;
}

bool StorageManager::ensureDirectories() {
    if (!isAvailable()) return false;
    SDCardManager& sd = SDCardManager::getInstance();
    sd.ensureDirectoryExists("/books");
    sd.ensureDirectoryExists("/offline");
    sd.ensureDirectoryExists("/covers");
    sd.ensureDirectoryExists("/cache");
    return true;
}

bool StorageManager::wipeAll() {
    if (!isAvailable()) return false;

    bool allRemoved = true;
    struct DirFrame {
        String path;
        bool expanded = false;
    };
    std::vector<DirFrame> stack;
    stack.push_back({String("/"), false});

    uint32_t lastYieldMs = millis();
    auto kickWdt = [&]() {
        const uint32_t now = millis();
        if (now - lastYieldMs >= 100) {
#ifdef ESP_PLATFORM
            if (esp_task_wdt_status(nullptr) == ESP_OK) {
                esp_task_wdt_reset();
            }
#endif
            delay(1);
            lastYieldMs = millis();
        }
    };

    while (!stack.empty()) {
        DirFrame frame = stack.back();
        stack.pop_back();

        if (!frame.expanded) {
            FsFile dir = open(frame.path.c_str(), O_RDONLY);
            if (!dir || !dir.isDirectory()) {
                if (dir) dir.close();
                allRemoved = false;
                continue;
            }

            std::vector<String> subdirs;
            char nameBuf[256];
            FsFile entry;
            while (entry.openNext(&dir, O_RDONLY)) {
                nameBuf[0] = '\0';
                entry.getName(nameBuf, sizeof(nameBuf));
                const bool isDir = entry.isDirectory();
                entry.close();

                if (nameBuf[0] == '\0' || strcmp(nameBuf, ".") == 0 || strcmp(nameBuf, "..") == 0) {
                    continue;
                }

                String fullPath = frame.path;
                if (!fullPath.endsWith("/")) fullPath += "/";
                fullPath += nameBuf;

                if (isDir) {
                    subdirs.push_back(fullPath);
                } else if (!remove(fullPath.c_str())) {
                    Serial.printf("[Storage] Wipe: failed to remove file %s\n", fullPath.c_str());
                    allRemoved = false;
                }
                kickWdt();
            }
            dir.close();

            if (frame.path != "/") {
                stack.push_back({frame.path, true});
            }
            for (const String& sub : subdirs) {
                stack.push_back({sub, false});
            }
        } else {
            if (!SDCardManager::getInstance().rmdir(frame.path.c_str())) {
                Serial.printf("[Storage] Wipe: failed to remove dir %s\n", frame.path.c_str());
                allRemoved = false;
            }
            kickWdt();
        }
    }

    ensureDirectories();
    return allRemoved;
}

FsFile StorageManager::open(const char* path, oflag_t oflag) { return SDCardManager::getInstance().open(path, oflag); }

bool StorageManager::exists(const char* path) { return SDCardManager::getInstance().exists(path); }

bool StorageManager::remove(const char* path) { return SDCardManager::getInstance().remove(path); }

bool StorageManager::rename(const char* path, const char* newPath) {
    return SDCardManager::getInstance().rename(path, newPath);
}

bool StorageManager::replaceFile(const char* tmpPath, const char* path) {
    return SDCardManager::getInstance().replaceFile(tmpPath, path);
}

bool StorageManager::mkdir(const char* path) { return SDCardManager::getInstance().mkdir(path); }

bool StorageManager::removeDir(const char* path) { return SDCardManager::getInstance().removeDir(path); }

std::vector<String> StorageManager::listSubdirs(const String& path) {
    std::vector<String> result;
    if (!isAvailable()) return result;
    FsFile dir = open(path.c_str(), O_RDONLY);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return result;
    }
    char nameBuf[256];
    FsFile entry;
    while (entry.openNext(&dir, O_RDONLY)) {
        if (entry.isDirectory()) {
            entry.getName(nameBuf, sizeof(nameBuf));
            result.push_back(String(nameBuf));
        }
        entry.close();
    }
    dir.close();
    return result;
}

std::vector<String> StorageManager::listFileNames(const String& path) {
    std::vector<String> result;
    if (!isAvailable()) return result;
    FsFile dir = open(path.c_str(), O_RDONLY);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return result;
    }
    char nameBuf[256];
    FsFile entry;
    while (entry.openNext(&dir, O_RDONLY)) {
        entry.getName(nameBuf, sizeof(nameBuf));
        if (!entry.isDirectory()) result.push_back(String(nameBuf));
        entry.close();
    }
    dir.close();
    return result;
}

std::vector<DirEntry> StorageManager::listDirectory(const String& path) {
    std::vector<DirEntry> result;
    if (!isAvailable()) return result;

    FsFile dir = open(path.c_str(), O_RDONLY);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return result;
    }

    char nameBuf[128];
    FsFile entry;
    while (entry.openNext(&dir, O_RDONLY)) {
        entry.getName(nameBuf, sizeof(nameBuf));
        String name(nameBuf);
        bool isDir = entry.isDirectory();
        size_t sz = entry.size();
        entry.close();

        // Skip hidden files, system folders, cache, and covers
        if (name.startsWith(".") || name.equalsIgnoreCase("System Volume Information") ||
            name.equalsIgnoreCase("cache") || name.equalsIgnoreCase("covers") ||
            name.equalsIgnoreCase("$RECYCLE.BIN")) {
            continue;
        }

        String fullPath = path;
        if (!fullPath.endsWith("/")) fullPath += "/";
        fullPath += name;

        if (isDir) {
            result.push_back({name, fullPath, true, 0});
        } else {
            // Every book format the library lists (EPUB, FB2, MOBI, AZW3, AZW, TXT).
            if (BookFormats::fromPath(name.c_str()) != BookFormats::Unknown) {
                result.push_back({name, fullPath, false, sz});
            }
        }
    }
    dir.close();

    // Sort: directories first (alphabetically), then files (alphabetically)
    std::sort(result.begin(), result.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
        }
        return a.name.compareTo(b.name) < 0;
    });

    return result;
}
