#include "../include/Arduino.h"
#include "../include/SDCardManager.h"
#include "../../src/net/bookorbit_client.h"
#include "../../src/net/web_config.h"
#include "../../src/core/ui_port.h"
#include "../../src/platform/hal/buttons.h"
#include "../../src/services/power_service.h"
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

BookOrbitClient BookOrbit;
WebConfigManager WebConfig;

BookOrbitClient::BookOrbitClient() {}

void BookOrbitClient::beginWiFi(const char* ssid, const char* pass) {
    (void)ssid;
    (void)pass;
}

bool BookOrbitClient::connectWiFi(const char* ssid, const char* pass, uint32_t timeoutMs, bool (*checkCancel)()) {
    Serial.printf("[SimNet] Connecting to simulated WiFi: %s\n", ssid ? ssid : "");
    for (int i = 0; i < 5; i++) {
        delay(60);
        if (checkCancel && checkCancel()) return false;
    }
    return true;
}

void BookOrbitClient::disconnectWiFi() { Serial.println("[SimNet] WiFi disconnected."); }

bool BookOrbitClient::isConnected() { return true; }

int BookOrbitClient::getRssi() { return -55; }

bool BookOrbitClient::pingServer(const String& serverHost) { return true; }

bool BookOrbitClient::login(const String& serverHost, const String& username, const String& password,
                            String& outToken) {
    outToken = "simulated_jwt_token_12345";
    return true;
}

bool BookOrbitClient::fetchCatalog(const String& serverHost, const String& username, const String& password,
                                   const String& token, std::vector<BookMetadata>& outBooks) {
    Serial.println("[SimNet] Fetching simulated OPDS catalog...");
    outBooks.clear();

    BookMetadata b1;
    b1.id = "1";
    b1.fileId = 101;
    b1.title = "Alice in Wonderland";
    b1.author = "Lewis Carroll";
    b1.formats.push_back(BookFormatEntry{101, 1 /* EPUB */, "/api/v1/download/101"});
    b1.formats.push_back(BookFormatEntry{111, 2 /* FB2 */, "/api/v1/download/111"});
    b1.coverUrl = "/api/v1/covers/101.jpg";
    b1.lastProgress = 0.42f;
    b1.lastPage = 145;
    outBooks.push_back(b1);

    BookMetadata b2;
    b2.id = "2";
    b2.fileId = 102;
    b2.title = "The Time Machine";
    b2.author = "H. G. Wells";
    b2.formats.push_back(BookFormatEntry{102, 1 /* EPUB */, "/api/v1/download/102"});
    b2.coverUrl = "/api/v1/covers/102.jpg";
    b2.lastProgress = 0.15f;
    b2.lastPage = 32;
    outBooks.push_back(b2);

    BookMetadata b3;
    b3.id = "3";
    b3.fileId = 103;
    b3.title = "Moby Dick";
    b3.author = "Herman Melville";
    b3.formats.push_back(BookFormatEntry{103, 1 /* EPUB */, "/api/v1/download/103"});
    b3.coverUrl = "/api/v1/covers/103.jpg";
    b3.lastProgress = 0.90f;
    b3.lastPage = 280;
    outBooks.push_back(b3);

    return true;
}

bool BookOrbitClient::downloadFile(const String& serverHost, const String& fileUrl, const String& destSdPath,
                                   const String& username, const String& password, const String& token,
                                   void (*onProgress)(int percent), bool (*checkCancel)()) {
    String cleanUrl = fileUrl;
    cleanUrl.replace("&amp;", "&");
    String fullUrl =
            cleanUrl.startsWith("http") ? cleanUrl : (serverHost + (cleanUrl.startsWith("/") ? "" : "/") + cleanUrl);
    std::string realDest = SDCardManager::getInstance().resolvePath(destSdPath.c_str());

    // Ensure target folder exists
    size_t lastSlash = realDest.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        std::string parentDir = realDest.substr(0, lastSlash);
        try {
            fs::create_directories(parentDir);
        } catch (...) {}
    }

    bool downloaded = false;

    // Try real HTTP download via curl if network URL
    if (fullUrl.startsWith("http://") || fullUrl.startsWith("https://")) {
        char cmd[1024];
        if (username.length() > 0 && password.length() > 0) {
            snprintf(cmd, sizeof(cmd), "curl -s -L -f --connect-timeout 4 -u \"%s:%s\" \"%s\" -o \"%s\"",
                     username.c_str(), password.c_str(), fullUrl.c_str(), realDest.c_str());
        } else if (token.length() > 0) {
            snprintf(cmd, sizeof(cmd),
                     "curl -s -L -f --connect-timeout 4 -H \"Authorization: Bearer %s\" \"%s\" -o \"%s\"",
                     token.c_str(), fullUrl.c_str(), realDest.c_str());
        } else {
            snprintf(cmd, sizeof(cmd), "curl -s -L -f --connect-timeout 4 \"%s\" -o \"%s\"", fullUrl.c_str(),
                     realDest.c_str());
        }
        int ret = system(cmd);
        if (ret == 0 && fs::exists(realDest) && fs::file_size(realDest) > 200) {
            downloaded = true;
            Serial.printf("[SimNet] Real book downloaded via curl: %s (%lu bytes)\n", realDest.c_str(),
                          (unsigned long)fs::file_size(realDest));
        }
    }

    // Fallback: copy a valid sample EPUB or text template
    if (!downloaded) {
        std::string sampleEpub = SDCardManager::getInstance().resolvePath("/books/Pride and Prejudice.epub");
        if (fs::exists(sampleEpub)) {
            try {
                fs::copy_file(sampleEpub, realDest, fs::copy_options::overwrite_existing);
                downloaded = true;
                Serial.printf("[SimNet] Created simulated book from template -> %s\n", realDest.c_str());
            } catch (...) {}
        }
    }

    if (onProgress) {
        for (int p = 0; p <= 100; p += 25) {
            onProgress(p);
            delay(40);
            if (checkCancel && checkCancel()) return false;
        }
    }
    return downloaded;
}

bool BookOrbitClient::downloadBook(const String& serverHost, const String& downloadUrl, const String& destSdPath,
                                   const String& username, const String& password, const String& token,
                                   void (*onProgress)(int percent), bool (*checkCancel)()) {
    return downloadFile(serverHost, downloadUrl, destSdPath, username, password, token, onProgress, checkCancel);
}

bool BookOrbitClient::downloadCover(const String& serverHost, const String& coverUrl, const String& destSdPath,
                                    const String& username, const String& password, const String& token,
                                    bool (*checkCancel)()) {
    String cleanUrl = coverUrl;
    cleanUrl.replace("&amp;", "&");
    String fullUrl =
            cleanUrl.startsWith("http") ? cleanUrl : (serverHost + (cleanUrl.startsWith("/") ? "" : "/") + cleanUrl);
    std::string realDest = SDCardManager::getInstance().resolvePath(destSdPath.c_str());

    size_t lastSlash = realDest.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        try {
            fs::create_directories(realDest.substr(0, lastSlash));
        } catch (...) {}
    }

    if (fullUrl.startsWith("http://") || fullUrl.startsWith("https://")) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "curl -s -L -f --connect-timeout 3 \"%s\" -o \"%s\"", fullUrl.c_str(),
                 realDest.c_str());
        system(cmd);
    }

    if (!fs::exists(realDest) || fs::file_size(realDest) < 50) {
        std::string sampleCover = SDCardManager::getInstance().resolvePath("/covers/Pride and Prejudice.jpg");
        if (fs::exists(sampleCover)) {
            try {
                fs::copy_file(sampleCover, realDest, fs::copy_options::overwrite_existing);
            } catch (...) {}
        }
    }
    return true;
}

bool BookOrbitClient::fetchBookProgress(const String& serverHost, const String& username, const String& password,
                                        String& token, int bookId, int fileId, ServerProgress& out) {
    out.exists = true;
    out.percent = 0.42f;
    out.page = 145;

    out.updatedAt = 1762464000;
    return true;
}

bool BookOrbitClient::updateBookProgress(const String& serverHost, const String& username, const String& password,
                                         String& token, int fileId, float percent, int page, const String& cfi,
                                         uint32_t* outUpdatedAt) {
    if (outUpdatedAt) *outUpdatedAt = (uint32_t)time(nullptr);
    Serial.printf("[SimNet] Progress updated on server: %.1f%% (page %d, cfi=%s)\n", percent * 100.0f, page,
                  cfi.c_str());
    return true;
}

bool BookOrbitClient::fetchBookmarks(const String&, const String&, const String&, String&, int, int,
                                     std::vector<BookOrbitProtocol::Bookmark>& out) {
    out.clear();
    return true;
}

int BookOrbitClient::createBookmark(const String&, const String&, const String&, String&, int, int, const String&,
                                    const String&) {
    return 1;
}

bool BookOrbitClient::deleteBookmark(const String&, const String&, const String&, String&, int, int) { return true; }

String BookOrbitClient::buildUrl(const String& host, const String& path) { return host + path; }

WebConfigManager::WebConfigManager() {}

bool WebConfigManager::runPortal(AppConfig& cfg, WebPortalMode mode) {
    Serial.printf("[SimNet] Simulated Web Portal opened (mode=%d).\n", (int)mode);
    const bool apMode = (mode == WebPortalMode::AccessPoint);
    const char* ssid = apMode ? "BookOrbit-Config" : cfg.wifiSsid.c_str();
    const char* pass = apMode ? "12345678" : "";
    const char* url = apMode ? "http://192.168.4.1/" : "http://192.168.1.42/";
    int page = apMode ? 0 : -1;
    Ui::portalScreen(ssid, pass, url, page);
    ButtonEvent event;
    while (true) {
        if (PowerService::pollModalEvent(event)) {
            const PortalAction act = Ui::portalEvent(event.id, event.action, event.x, event.y, page);
            if (act == PortalAction::Close) break;
            if (act == PortalAction::Redraw) {
                Ui::portalScreen(ssid, pass, url, page);
            }
        }
    }
    Serial.println("[SimNet] Simulated Web Portal closed.");
    return false;
}
