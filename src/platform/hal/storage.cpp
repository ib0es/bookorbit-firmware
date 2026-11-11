#include "storage.h"
#include <algorithm>
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

void StorageManager::shutdown() {
    SDCardManager::getInstance().shutdown();
}

FsBlockDeviceInterface* StorageManager::detachForUsbMsc() {
    return SDCardManager::getInstance().detachFilesystemForRawAccess();
}

bool StorageManager::isAvailable() const {
    return SDCardManager::getInstance().ready();
}

uint64_t StorageManager::totalBytes() const {
    return SDCardManager::getInstance().sdTotalBytes();
}

bool StorageManager::ensureDirectories() {
    if (!isAvailable()) return false;
    SDCardManager& sd = SDCardManager::getInstance();
    sd.ensureDirectoryExists("/books");
    sd.ensureDirectoryExists("/covers");
    sd.ensureDirectoryExists("/cache");
    return true;
}

FsFile StorageManager::open(const char* path, oflag_t oflag) {
    return SDCardManager::getInstance().open(path, oflag);
}

bool StorageManager::exists(const char* path) {
    return SDCardManager::getInstance().exists(path);
}

bool StorageManager::remove(const char* path) {
    return SDCardManager::getInstance().remove(path);
}

bool StorageManager::rename(const char* path, const char* newPath) {
    return SDCardManager::getInstance().rename(path, newPath);
}

bool StorageManager::replaceFile(const char* tmpPath, const char* path) {
    return SDCardManager::getInstance().replaceFile(tmpPath, path);
}

bool StorageManager::mkdir(const char* path) {
    return SDCardManager::getInstance().mkdir(path);
}

bool StorageManager::removeDir(const char* path) {
    return SDCardManager::getInstance().removeDir(path);
}

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
            result.push_back({ name, fullPath, true, 0 });
        } else {
            // Every book format the library lists (EPUB, FB2, MOBI, AZW3, AZW, TXT).
            if (BookFormats::fromPath(name.c_str()) != BookFormats::Unknown) {
                result.push_back({ name, fullPath, false, sz });
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
