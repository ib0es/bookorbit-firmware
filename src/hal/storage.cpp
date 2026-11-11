#include "storage.h"
#include "../ui/font.h"
#include "../app/app_state.h"
#include <ArduinoJson.h>
#include <algorithm>
#include "../net/bookorbit_client.h"
#include "../net/book_formats.h"
#include "../storage/json_file.h"
#include "../platform/device.h"
#include <Preferences.h>

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

bool StorageManager::loadConfig(AppConfig& cfg) {
    bool sdLoaded = false;
    if (isAvailable()) {
        {
            JsonDocument doc;
            DeserializationError error = JsonFile::read("/config.json", doc);
            if (!error) {
                if (doc["wifi_ssid"].is<const char*>()) { cfg.wifiSsid = doc["wifi_ssid"].as<String>(); cfg.wifiSsid.trim(); }
                // Passwords as typed: spaces in them are significant (names and addresses are trimmed).
                if (doc["wifi_pass"].is<const char*>()) cfg.wifiPass = doc["wifi_pass"].as<String>();
                if (doc["server_url"].is<const char*>()) { cfg.serverHost = doc["server_url"].as<String>(); cfg.serverHost.trim(); }

                if (doc["username"].is<const char*>()) { cfg.bookOrbitUser = doc["username"].as<String>(); cfg.bookOrbitUser.trim(); }
                else if (doc["user"].is<const char*>()) { cfg.bookOrbitUser = doc["user"].as<String>(); cfg.bookOrbitUser.trim(); }

                if (doc["password"].is<const char*>()) cfg.bookOrbitPass = doc["password"].as<String>();
                else if (doc["pass"].is<const char*>()) cfg.bookOrbitPass = doc["pass"].as<String>();

                if (doc["token"].is<const char*>()) { cfg.authToken = doc["token"].as<String>(); cfg.authToken.trim(); }

                if (doc["sync_on_boot"].is<bool>()) cfg.syncOnBoot = doc["sync_on_boot"].as<bool>();
                if (doc["sync_on_sleep"].is<bool>()) cfg.syncOnSleep = doc["sync_on_sleep"].as<bool>();
                if (doc["sync_on_close"].is<bool>()) cfg.syncOnClose = doc["sync_on_close"].as<bool>();
                if (doc["sync_on_wake"].is<bool>()) cfg.syncOnWake = doc["sync_on_wake"].as<bool>();
                if (doc["cover_on_sleep"].is<bool>()) cfg.coverOnSleep = doc["cover_on_sleep"].as<bool>();
                if (doc["frontlight"].is<int>()) cfg.frontlight = doc["frontlight"].as<int>();
                if (doc["frontlight_warm"].is<int>()) cfg.frontlightWarm = doc["frontlight_warm"].as<int>();
                if (doc["hide_completed"].is<bool>()) cfg.hideCompleted = doc["hide_completed"].as<bool>();

                if (doc["cover_fit_mode"].is<int>()) {
                    int v = doc["cover_fit_mode"].as<int>();
                    cfg.sleepCoverFitMode = (v < 0 || v > 1) ? 0 : v;
                } else {
                    cfg.sleepCoverFitMode = 0;
                }
                // Book font size: "font_pt" = 12..48 (step 2). Older firmware stored
                // "font_size" = 0..3 for its four fonts (13, 15, 20, 24 px): mapped to the
                // nearest new size.
                if (doc["font_pt"].is<int>()) {
                    cfg.readerFontSize = FontEngine::clampSize((doc["font_pt"].as<int>() - 12) / 2);
                } else if (doc["font_size"].is<int>()) {
                    static const int kOld[4] = { 0, 2, 4, 6 };   // -> 12, 16, 20, 24 pt
                    const int v = doc["font_size"].as<int>();
                    cfg.readerFontSize = (v >= 0 && v <= 3) ? kOld[v] : FontEngine::kDefaultSize;
                } else {
                    cfg.readerFontSize = FontEngine::kDefaultSize;
                }

                cfg.readerFont = doc["font_family"].is<const char*>() ? doc["font_family"].as<String>() : String();
                auto intIn = [&](const char* key, int& v, int lo, int hi) {
                    if (doc[key].is<int>()) v = constrain(doc[key].as<int>(), lo, hi);
                };
                auto boolIn = [&](const char* key, bool& v) {
                    if (doc[key].is<bool>()) v = doc[key].as<bool>();
                };
                intIn("line_spacing", cfg.lineSpacing, 80, 200);
                intIn("paragraph_spacing", cfg.paragraphSpacing, 0, 200);
                intIn("margin_px", cfg.marginPx, 4, 60);
                intIn("full_refresh_every", cfg.fullRefreshEvery, 0, 50);
                boolIn("justify", cfg.justify);
                boolIn("hyphenation", cfg.hyphenation);
                boolIn("keep_lines", cfg.keepLines);
                boolIn("book_styles", cfg.bookStyles);
                boolIn("focus_reading", cfg.focusReading);
                boolIn("sharp_images", cfg.sharpImages);
                boolIn("updown_pages", cfg.upDownTurnPages);
                intIn("orientation", cfg.orientation, 0, 3);
                intIn("ui_orientation", cfg.uiOrientation, 0, 3);
                boolIn("auto_rotate", cfg.autoRotate);
                intIn("imu_rot", cfg.imuRot, 0, 3);
                boolIn("imu_flip_x", cfg.imuFlipX);
                boolIn("imu_flip_y", cfg.imuFlipY);
                intIn("sb_top", cfg.sbTop, 0, 2);
                intIn("sb_pages", cfg.sbPages, 0, 2);
                intIn("sb_bottom", cfg.sbBottom, 0, 2);
                boolIn("sb_percent", cfg.sbPercent);
                boolIn("sb_battery", cfg.sbBattery);
                boolIn("sb_clock", cfg.sbClock);
                intIn("utc_offset_min", cfg.utcOffsetMin, -720, 840);
                boolIn("usb_msc", cfg.usbMscEnabled);
                boolIn("touch_wake", cfg.touchWakeEnabled);

                if (doc["language"].is<const char*>()) {
                    cfg.language = I18n::languageFromCode(doc["language"].as<String>());
                } else {
                    cfg.language = I18n::languageFromCode(DEFAULT_LANGUAGE);
                }

                Serial.println("[Storage] Config successfully loaded from MicroSD.");
                sdLoaded = true;
            } else if (error != DeserializationError::NoMemory && error != DeserializationError::EmptyInput) {
                JsonFile::keepUnreadable("/config.json");   // a new one is written from the NVS copy below
            }
        }
    }

    // No config.json on the card (new or formatted card): the network and
    // account settings come from their NVS copy. When the card has a config,
    // it is the truth, so a cleared field (Forget network) stays cleared.
    if (!sdLoaded) {
        Preferences prefs;
        if (prefs.begin("bookorbit", true)) {
            cfg.serverHost = prefs.getString("server_url", cfg.serverHost);
            cfg.wifiSsid = prefs.getString("wifi_ssid", cfg.wifiSsid);
            cfg.wifiPass = prefs.getString("wifi_pass", cfg.wifiPass);
            cfg.bookOrbitUser = prefs.getString("username", cfg.bookOrbitUser);
            cfg.bookOrbitPass = prefs.getString("password", cfg.bookOrbitPass);
            cfg.authToken = prefs.getString("token", cfg.authToken);
            prefs.end();
            Serial.println("[Storage] No config on the card: network settings taken from NVS.");
        }
    }

    // Auto-create /config.json on SD card if it didn't exist but credentials exist in NVS
    if (!sdLoaded && isAvailable() && (cfg.serverHost.length() > 0 || cfg.wifiSsid.length() > 0)) {
        saveConfig(cfg);
    }

    return sdLoaded || (cfg.serverHost.length() > 0 || cfg.wifiSsid.length() > 0);
}

bool StorageManager::saveConfig(const AppConfig& cfg) {
    // Mirror network and auth settings to NVS so they survive a card format or
    // swap. Every field is mirrored, empty ones too (as a removed key), and only
    // when it changed: config.json is saved often (font, light, toggles).
    Preferences prefs;
    if (prefs.begin("bookorbit", false)) {
        const struct { const char* key; const String& value; } fields[] = {
            { "server_url", cfg.serverHost }, { "wifi_ssid", cfg.wifiSsid }, { "wifi_pass", cfg.wifiPass },
            { "username", cfg.bookOrbitUser }, { "password", cfg.bookOrbitPass }, { "token", cfg.authToken } };
        for (const auto& f : fields) {
            if (f.value.length() == 0) {
                if (prefs.isKey(f.key)) prefs.remove(f.key);
            } else if (prefs.getString(f.key, "") != f.value) {
                prefs.putString(f.key, f.value);
            }
        }
        prefs.end();
    }

    if (!isAvailable()) return false;

    JsonDocument doc;
    doc["wifi_ssid"] = cfg.wifiSsid;
    doc["wifi_pass"] = cfg.wifiPass;
    doc["server_url"] = cfg.serverHost;
    doc["username"] = cfg.bookOrbitUser;
    doc["password"] = cfg.bookOrbitPass;
    doc["token"] = cfg.authToken;
    doc["sync_on_boot"] = cfg.syncOnBoot;
    doc["sync_on_sleep"] = cfg.syncOnSleep;
    doc["sync_on_close"] = cfg.syncOnClose;
    doc["sync_on_wake"] = cfg.syncOnWake;
    doc["cover_on_sleep"] = cfg.coverOnSleep;
    doc["frontlight"] = cfg.frontlight;
    doc["frontlight_warm"] = cfg.frontlightWarm;
    doc["hide_completed"] = cfg.hideCompleted;
    doc["cover_fit_mode"] = cfg.sleepCoverFitMode;
    doc["font_pt"] = FontEngine::sizePx(cfg.readerFontSize);
    doc["font_family"] = cfg.readerFont;
    doc["line_spacing"] = cfg.lineSpacing;
    doc["paragraph_spacing"] = cfg.paragraphSpacing;
    doc["margin_px"] = cfg.marginPx;
    doc["justify"] = cfg.justify;
    doc["hyphenation"] = cfg.hyphenation;
    doc["keep_lines"] = cfg.keepLines;
    doc["book_styles"] = cfg.bookStyles;
    doc["focus_reading"] = cfg.focusReading;
    doc["sharp_images"] = cfg.sharpImages;
    doc["updown_pages"] = cfg.upDownTurnPages;
    doc["orientation"] = cfg.orientation;
    doc["ui_orientation"] = cfg.uiOrientation;
    doc["auto_rotate"] = cfg.autoRotate;
    doc["imu_rot"] = cfg.imuRot;
    doc["imu_flip_x"] = cfg.imuFlipX;
    doc["imu_flip_y"] = cfg.imuFlipY;
    doc["sb_top"] = cfg.sbTop;
    doc["sb_pages"] = cfg.sbPages;
    doc["sb_bottom"] = cfg.sbBottom;
    doc["sb_percent"] = cfg.sbPercent;
    doc["sb_battery"] = cfg.sbBattery;
    doc["sb_clock"] = cfg.sbClock;
    doc["utc_offset_min"] = cfg.utcOffsetMin;
    doc["usb_msc"] = cfg.usbMscEnabled;
    doc["touch_wake"] = cfg.touchWakeEnabled;
    doc["full_refresh_every"] = cfg.fullRefreshEvery;
    doc["language"] = I18n::getLanguageCode(cfg.language);
    const bool ok = JsonFile::write("/config.json", doc, /*pretty=*/true);
    if (ok) {
        Serial.println("[Storage] Config saved to MicroSD.");
    }
    return ok;
}

bool StorageManager::saveCatalog(const std::vector<BookMetadata>& catalog) {
    if (!isAvailable()) return false;

    ensureDirectories();

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (const auto& b : catalog) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = b.id;
        obj["fileId"] = b.fileId;
        obj["title"] = b.title;
        obj["author"] = b.author;
        if (b.series.length() > 0) obj["series"] = b.series;
        if (b.seriesIndex > 0) obj["seriesIdx"] = b.seriesIndex;
        if (b.library.length() > 0) obj["library"] = b.library;
        // Formats: [{"f":fileId,"k":kind,"u":url}], "u" only when not canonical.
        JsonArray fa = obj["formats"].to<JsonArray>();
        for (const auto& f : b.formats) {
            JsonObject fo = fa.add<JsonObject>();
            fo["f"] = f.fileId;
            fo["k"] = f.kind;
            if (f.url.length() > 0) fo["u"] = f.url;
        }
        obj["cover"] = b.coverUrl;
    }
    const bool ok = JsonFile::write("/books/catalog.json", doc);
    if (ok) {
        Serial.printf("[Storage] Catalog metadata saved (%d books) to /books/catalog.json\n", (int)catalog.size());
    }
    return ok;
}

bool StorageManager::loadCatalog(std::vector<BookMetadata>& outCatalog) {
    outCatalog.clear();
    if (!isAvailable()) return false;

    JsonDocument doc;
    if (JsonFile::read("/books/catalog.json", doc)) return false;   // the next sync writes a new one

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        BookMetadata b;
        if (obj["id"].is<const char*>()) b.id = obj["id"].as<String>();
        if (obj["fileId"].is<int>()) b.fileId = obj["fileId"].as<int>();
        if (obj["title"].is<const char*>()) b.title = obj["title"].as<String>();
        if (obj["author"].is<const char*>()) b.author = obj["author"].as<String>();
        if (obj["series"].is<const char*>()) b.series = obj["series"].as<String>();
        if (obj["seriesIdx"].is<int>()) b.seriesIndex = obj["seriesIdx"].as<int>();
        if (obj["library"].is<const char*>()) b.library = obj["library"].as<String>();
        if (obj["formats"].is<JsonArray>()) {
            for (JsonObject fo : obj["formats"].as<JsonArray>()) {
                BookFormatEntry f;
                f.fileId = fo["f"] | 0;
                f.kind = fo["k"] | 0;
                if (fo["u"].is<const char*>()) f.url = fo["u"].as<String>();
                if (f.fileId > 0 || f.url.length() > 0) b.formats.push_back(f);
            }
        } else if (obj["url"].is<const char*>()) {
            // Catalog of older firmware: one address per book (EPUB, or TXT by its name).
            BookFormatEntry f;
            f.fileId = b.fileId;
            f.url = obj["url"].as<String>();
            f.kind = (uint8_t)(f.url.indexOf(".txt") > 0 ? BookFormats::Txt : BookFormats::Epub);
            b.formats.push_back(f);
        }
        if (obj["cover"].is<const char*>()) b.coverUrl = obj["cover"].as<String>();
        // "progress", "page", "xpath", "srvUpdated" of older firmware: server
        // snapshots of books not on the card, no longer kept.
        if (b.title.length() > 0) {
            outCatalog.push_back(b);
        }
    }

    Serial.printf("[Storage] Loaded %d books metadata from /books/catalog.json\n", (int)outCatalog.size());
    return true;
}

bool StorageManager::saveLastBook(const LastBookInfo& info) {
    if (!isAvailable()) return false;

    ensureDirectories();

    JsonDocument doc;
    doc["filePath"] = info.filePath;
    doc["title"] = info.title;
    doc["author"] = info.author;
    doc["coverPath"] = info.coverPath;
    doc["progress"] = info.progress;
    doc["page"] = info.page;
    doc["totalPages"] = info.totalPages;
    if (info.fileId > 0) doc["fileId"] = info.fileId;
    if (info.docId.length() > 0) doc["docId"] = info.docId;
    if (info.xpath.length() > 0) doc["xpath"] = info.xpath;
    const bool ok = JsonFile::write("/books/last_book.json", doc);
    if (ok) {
        Serial.printf("[Storage] Last read book saved: %s (page %d)\n", info.title.c_str(), info.page);
    }
    return ok;
}

bool StorageManager::loadLastBook(LastBookInfo& outInfo) {
    if (!isAvailable()) return false;

    JsonDocument doc;
    if (JsonFile::read("/books/last_book.json", doc)) return false;

    if (doc["filePath"].is<const char*>()) outInfo.filePath = doc["filePath"].as<String>();
    if (doc["title"].is<const char*>()) outInfo.title = doc["title"].as<String>();
    if (doc["author"].is<const char*>()) outInfo.author = doc["author"].as<String>();
    if (doc["coverPath"].is<const char*>()) outInfo.coverPath = doc["coverPath"].as<String>();
    if (doc["progress"].is<float>()) outInfo.progress = doc["progress"].as<float>();
    if (doc["page"].is<int>()) outInfo.page = doc["page"].as<int>();
    if (doc["totalPages"].is<int>()) outInfo.totalPages = doc["totalPages"].as<int>();
    if (doc["fileId"].is<int>()) outInfo.fileId = doc["fileId"].as<int>();
    if (doc["docId"].is<const char*>()) outInfo.docId = doc["docId"].as<String>();
    if (doc["xpath"].is<const char*>()) outInfo.xpath = doc["xpath"].as<String>();

    Serial.printf("[Storage] Last read book loaded: %s (page %d/%d)\n", 
                  outInfo.title.c_str(), outInfo.page, outInfo.totalPages);
    return (outInfo.filePath.length() > 0);
}
