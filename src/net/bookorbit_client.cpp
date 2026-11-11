#include "bookorbit_client.h"
#include <ArduinoJson.h>
#include "../hal/storage.h"
#include "../core/opds_stream_parser.h"
#include "../core/bookorbit_protocol.h"
#include "../services/sync_clock.h"
#include "../core/time_utils.h"
#include "config.h"
#include <base64.h>
#include <esp_wifi.h>

BookOrbitClient BookOrbit;

namespace {
const char* kCollectedHeaders[] = {"Date"};

// Asks HTTPClient to keep the Date header (call before the request).
void collectDate(HTTPClient& http) {
    http.collectHeaders(kCollectedHeaders, 1);
}

// Sets the device clock from the response Date header (call after the request).
void takeServerTime(HTTPClient& http) {
    SyncClock::setFromHttpDate(http.header("Date"));
}
}  // namespace

BookOrbitClient::BookOrbitClient() {}

String BookOrbitClient::buildUrl(const String& host, const String& path) {
    String h = host;
    h.trim();
    if (!h.startsWith("http://") && !h.startsWith("https://")) {
        h = "http://" + h;
    }
    if (h.endsWith("/")) h.remove(h.length() - 1);
    String p = path;
    p.trim();
    if (!p.startsWith("/")) p = "/" + p;
    return h + p;
}

String BookOrbitClient::urlEncode(const String& str) {
    String encoded = "";
    const char* hex = "0123456789ABCDEF";
    for (size_t i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0xF];
            encoded += hex[c & 0xF];
        }
    }
    return encoded;
}

bool BookOrbitClient::connectWiFi(const char* ssid, const char* pass, uint32_t timeoutMs, bool (*checkCancel)()) {
    if (!ssid || strlen(ssid) == 0) return false;
    if (_foregroundHook) _foregroundHook();

    // Fast path: only if legitimately connected to the requested SSID with a valid IP
    if (isConnected() && WiFi.SSID().equals(ssid)) {
        WiFi.setSleep(false);
        esp_wifi_set_ps(WIFI_PS_NONE);
        Serial.printf("[WiFi] Already connected to %s. IP: %s | RSSI: %d dBm\n", 
                      ssid, WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }

    Serial.printf("[WiFi] Initializing STA mode and connecting to %s...\n", ssid);
    // 1. Must set mode to WIFI_STA first so the driver is initialized before other commands
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    delay(40);

    // 2. Disconnect any stale or hanging associations
    WiFi.disconnect(false);
    delay(40);

    // 3. Begin new connection
    WiFi.begin(ssid, pass);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (checkCancel && checkCancel()) {
            Serial.println("\n[WiFi] Connection aborted by user!");
            disconnectWiFi();
            return false;
        }
        delay(100);
        Serial.print(".");
        if (millis() - start > timeoutMs) {
            Serial.println("\n[WiFi] Connection timeout!");
            disconnectWiFi();
            return false;
        }
    }

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    Serial.printf("\n[WiFi] Connected! IP: %s | RSSI: %d dBm\n", 
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
}

void BookOrbitClient::beginWiFi(const char* ssid, const char* pass) {
    if (!ssid || !*ssid || (isConnected() && WiFi.SSID().equals(ssid))) return;
    Serial.printf("[WiFi] Connecting to %s in the background...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.begin(ssid, pass);
}

void BookOrbitClient::disconnectWiFi() {
    Serial.println("[WiFi] Disconnecting radio for energy saving...");
    WiFi.disconnect(true);
    delay(50);
    WiFi.mode(WIFI_OFF);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}

bool BookOrbitClient::isConnected() {
    return (WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA) &&
           (WiFi.status() == WL_CONNECTED) &&
           (WiFi.localIP() != IPAddress(0, 0, 0, 0));
}

int BookOrbitClient::getRssi() {
    return isConnected() ? WiFi.RSSI() : 0;
}

bool BookOrbitClient::pingServer(const String& serverHost) {
    if (!isConnected()) return false;

    HTTPClient http;
    String url = buildUrl(serverHost, "/api/v1/health");
    http.begin(url);
    http.setTimeout(4000);
    http.addHeader("Connection", "close");

    int code = http.GET();
    http.end();

    Serial.printf("[BookOrbit] Health check (%s) -> HTTP %d\n", url.c_str(), code);
    return (code >= 200 && code < 400) || (code == 401);
}

bool BookOrbitClient::login(const String& serverHost, const String& username, const String& password, String& outToken) {
    if (!isConnected()) return false;
    if (username.length() == 0 || password.length() == 0) return false;

    HTTPClient http;
    String url = buildUrl(serverHost, "/api/v1/auth/login");
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Connection", "close");
    http.setTimeout(8000);

    JsonDocument doc;
    doc["username"] = username;
    doc["password"] = password;

    String body;
    serializeJson(doc, body);

    Serial.printf("[BookOrbit] Authenticating as '%s' at %s...\n", username.c_str(), url.c_str());
    collectDate(http);
    int code = http.POST(body);
    takeServerTime(http);
    if (code != 200 && code != 201) {
        if (code < 0) {
            Serial.printf("[BookOrbit] Login network error: %s\n", http.errorToString(code).c_str());
        } else {
            Serial.printf("[BookOrbit] Login failed with HTTP %d\n", code);
        }
        http.end();
        return false;
    }

    String response = http.getString();
    http.end();

    std::string tok;
    BookOrbitProtocol::parseLoginToken(response.c_str(), tok);
    outToken = tok.c_str();

    if (outToken.length() > 0) {
        Serial.println("[BookOrbit] Authentication successful! Access token acquired.");
        return true;
    }

    Serial.println("[BookOrbit] Login response did not contain access token!");
    return false;
}

namespace {

// Adapts HTTPClient::writeToStream() (which also handles chunked transfer
// encoding) to the streaming OPDS parser: bytes are parsed as they arrive and
// never accumulated into one big String.
class OpdsParserSink : public Stream {
public:
    explicit OpdsParserSink(OpdsStreamParser& parser) : _parser(parser) {}
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* buf, size_t size) override {
        _bytes += size;
        // Report the chunk as consumed even after the parser stopped, so the
        // HTTP client finishes the transfer cleanly instead of erroring out.
        _parser.feed(reinterpret_cast<const char*>(buf), size);
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t bytes() const { return _bytes; }

private:
    OpdsStreamParser& _parser;
    size_t _bytes = 0;
};

// Buffers the HTTP body and writes it to the SD card in large blocks.
class FileDownloadSink : public Stream {
public:
    FileDownloadSink(FsFile& file, uint8_t* buf, size_t bufSize, int total,
                     void (*onProgress)(int), bool (*checkCancel)())
        : _file(file), _buf(buf), _size(bufSize), _total(total), _onProgress(onProgress), _checkCancel(checkCancel) {}
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t* data, size_t len) override {
        if (_failed) return 0;
        if (_checkCancel && _checkCancel()) { _cancelled = _failed = true; return 0; }   // aborts writeToStream
        size_t done = 0;
        while (done < len) {
            size_t n = len - done;
            if (n > _size - _used) n = _size - _used;
            memcpy(_buf + _used, data + done, n);
            _used += n;
            done += n;
            if (_used == _size && !flush_()) return 0;
        }
        _bytes += len;
        if (_total > 0 && _onProgress) {
            const int pct = (int)((uint64_t)_bytes * 100 / (uint64_t)_total);
            if (pct != _lastPct) { _lastPct = pct; _onProgress(pct); }
        }
        return len;
    }
    bool finish() { return !_failed && flush_(); }
    size_t bytes() const { return _bytes; }
    bool cancelled() const { return _cancelled; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}

private:
    bool flush_() {
        if (_used == 0) return true;
        const size_t w = _file.write(_buf, _used);
        if (w != _used) {
            Serial.printf("[BookOrbit] SD card write failed: wrote %u of %u bytes!\n", (unsigned)w, (unsigned)_used);
            _failed = true;
            return false;
        }
        _used = 0;
        return true;
    }
    FsFile& _file;
    uint8_t* _buf;
    size_t _size, _used = 0, _bytes = 0;
    int _total, _lastPct = -1;
    void (*_onProgress)(int);
    bool (*_checkCancel)();
    bool _failed = false, _cancelled = false;
};

}  // namespace

bool BookOrbitClient::fetchLibraries(const String& serverHost, const String& username, const String& password, const String& token, std::vector<ServerLibrary>& outLibraries) {
    outLibraries.clear();
    if (!isConnected()) return false;

    HTTPClient http;
    String url = buildUrl(serverHost, "/api/v1/opds/libraries");
    http.begin(url);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    http.addHeader("Connection", "close");

    if (username.length() > 0 && password.length() > 0) {
        String auth = base64::encode(username + ":" + password);
        http.addHeader("Authorization", "Basic " + auth);
    } else if (token.length() > 0) {
        http.addHeader("Authorization", "Bearer " + token);
    }

    int code = http.GET();
    Serial.printf("[BookOrbit] fetchLibraries: GET %s -> HTTP %d\n", url.c_str(), code);
    if (code != 200) {
        Serial.printf("[BookOrbit] fetchLibraries failed (HTTP %d)\n", code);
        http.end();
        return false;
    }

    // The navigation feed is small (one entry per library); refuse an oversized
    // one instead of loading it whole into a heap that can be ~15 KB here.
    constexpr int kMaxNavBytes = 16 * 1024;
    const int size = http.getSize();
    if (size > kMaxNavBytes) {
        Serial.printf("[BookOrbit] fetchLibraries: feed too large (%d bytes), using the full catalog\n", size);
        http.end();
        return false;
    }
    std::string xml;
    xml.reserve(size > 0 ? size : 2048);
    WiFiClient* stream = http.getStreamPtr();
    const uint32_t tStart = millis();
    while (http.connected() && (size < 0 || (int)xml.size() < size) && millis() - tStart < 15000) {
        const int avail = stream ? stream->available() : 0;
        if (avail <= 0) { delay(2); continue; }
        char buf[256];
        const int n = stream->readBytes(buf, avail < (int)sizeof(buf) ? avail : (int)sizeof(buf));
        if (n <= 0) break;
        if ((int)xml.size() + n > kMaxNavBytes) {
            Serial.println("[BookOrbit] fetchLibraries: feed too large, using the full catalog");
            http.end();
            return false;
        }
        xml.append(buf, n);
    }
    http.end();

    auto navs = OpdsStreamParser::parseNavigationFeed(xml);
    for (const auto& n : navs) {
        ServerLibrary lib;
        lib.name = n.title.c_str();
        lib.href = n.href.c_str();
        size_t colon = n.id.rfind(':');
        if (colon != std::string::npos) {
            lib.id = atoi(n.id.c_str() + colon + 1);
        }
        if (lib.id <= 0) {
            size_t libParam = n.href.find("libraryId=");
            if (libParam != std::string::npos) lib.id = atoi(n.href.c_str() + libParam + 10);
        }
        lib.bookCount = atoi(n.content.c_str());
        outLibraries.push_back(lib);
        Serial.printf("[BookOrbit] Discovered library: '%s' (id=%d, count=%d, href=%s)\n",
                      lib.name.c_str(), lib.id, lib.bookCount, lib.href.c_str());
    }
    return !outLibraries.empty();
}

bool BookOrbitClient::fetchCatalogFromUrl(const String& serverHost, const String& initialUrl, const String& username, const String& password, const String& token, const String& libraryName, std::vector<BookMetadata>& outBooks) {
    uint32_t t0 = millis();
    bool limitReached = false;
    String pageUrl = initialUrl;

    for (int pageNo = 1; pageNo <= OPDS_MAX_PAGES && pageUrl.length() > 0; pageNo++) {
        HTTPClient http;
        http.begin(pageUrl);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(15000);
        http.addHeader("Connection", "close");

        // Book Orbit OPDS requires HTTP Basic Auth:
        if (username.length() > 0 && password.length() > 0) {
            String auth = base64::encode(username + ":" + password);
            http.addHeader("Authorization", "Basic " + auth);
        } else if (token.length() > 0) {
            http.addHeader("Authorization", "Bearer " + token);
        }

        uint32_t tPage = millis();
        int code = http.GET();
        if (code != 200 || lastCatalog.httpCode == 0) lastCatalog.httpCode = code;   // keep the first failure
        if (code != 200) {
            Serial.printf("[BookOrbit] OPDS catalog fetch (%s) failed with HTTP %d (took %lu ms)\n",
                          pageUrl.c_str(), code, millis() - tPage);
            http.end();
            // This feed is incomplete: the saved catalog fills in what is missing.
            lastCatalog.truncated = true;
            return false;
        }

        OpdsStreamParser parser([&](const OpdsBookEntry& e) {
            const uint32_t heap = ESP.getFreeHeap();
            if (heap < lastCatalog.heapMin) lastCatalog.heapMin = heap;
            // The heap guard never empties the catalog: one entry costs ~1-2 KB.
            if (outBooks.size() >= OPDS_MAX_BOOKS || (!outBooks.empty() && heap < OPDS_MIN_FREE_HEAP)) {
                limitReached = true;
                lastCatalog.truncated = true;
                return false;
            }
            BookMetadata b;
            b.id = e.id.c_str();
            b.fileId = e.fileId;
            b.title = e.title.c_str();
            b.author = e.author.c_str();
            b.series = e.series.c_str();
            b.seriesIndex = e.seriesIndex;
            b.formats.reserve(e.formats.size());
            for (const auto& f : e.formats) {
                BookFormatEntry fe;
                fe.fileId = f.fileId;
                fe.kind = (uint8_t)f.kind;
                fe.url = f.url.c_str();
                b.formats.push_back(fe);
            }
            b.coverUrl = e.coverUrl.c_str();
            b.library = libraryName;
            outBooks.push_back(b);
            Serial.printf("[OPDS] Found book: %s (id=%s, fileId=%d, %u formats, lib=%s)\n", b.title.c_str(), b.id.c_str(),
                          b.fileId, (unsigned)b.formats.size(), b.library.c_str());
            return true;
        }, 8 * 1024);   // entries are ~1-3 KB; the heap can be ~15 KB here
        OpdsParserSink sink(parser);
        int written = http.writeToStream(&sink);
        http.end();
        lastCatalog.bytes += sink.bytes();
        lastCatalog.skipped += parser.entriesSkipped();
        if (written < 0) lastCatalog.streamError = written;

        Serial.printf("[BookOrbit] OPDS page %d: %u bytes, %u books, %u skipped entries, %lu ms (free heap %u)\n",
                      pageNo, (unsigned)sink.bytes(), (unsigned)parser.booksParsed(),
                      (unsigned)parser.entriesSkipped(), millis() - tPage, (unsigned)ESP.getFreeHeap());

        if (written < 0 && !parser.stopped()) {
            Serial.printf("[BookOrbit] OPDS stream error %d on page %d\n", written, pageNo);
            lastCatalog.truncated = true;
            return false;
        }
        if (limitReached) break;

        // Pagination: follow rel="next" (absolute or server-relative).
        String next = parser.nextPageUrl().c_str();
        if (next.length() == 0 || next == pageUrl) break;
        pageUrl = (next.startsWith("http://") || next.startsWith("https://")) ? next : buildUrl(serverHost, next);
    }

    if (limitReached) {
        Serial.printf("[BookOrbit] Catalog truncated at %u books (limit %u, free heap %u).\n",
                      (unsigned)outBooks.size(), (unsigned)OPDS_MAX_BOOKS, (unsigned)ESP.getFreeHeap());
    }
    Serial.printf("[BookOrbit] Parsed %d books from OPDS feed '%s' in %lu ms total (heap start %u, min %u).\n",
                  (int)outBooks.size(), initialUrl.c_str(), millis() - t0, (unsigned)lastCatalog.heapStart, (unsigned)lastCatalog.heapMin);
    return true;
}

bool BookOrbitClient::fetchCatalog(const String& serverHost, const String& username, const String& password, const String& token, std::vector<BookMetadata>& outBooks) {
    if (!isConnected()) return false;

    outBooks.clear();
    lastCatalog = CatalogFetchInfo();
    lastCatalog.heapStart = lastCatalog.heapMin = ESP.getFreeHeap();

    std::vector<ServerLibrary> libs;
    fetchLibraries(serverHost, username, password, token, libs);

    if (!libs.empty()) {
        Serial.printf("[BookOrbit] Found %d server libraries. Fetching catalog per library...\n", (int)libs.size());
        int failed = 0;
        for (const auto& lib : libs) {
            String initialUrl = lib.href.startsWith("http") ? lib.href : buildUrl(serverHost, lib.href);
            if (!fetchCatalogFromUrl(serverHost, initialUrl, username, password, token, lib.name, outBooks)) failed++;
        }
        // A library that failed (or stopped part-way) marks the catalog incomplete
        // (lastCatalog.truncated): the sync then keeps its books from the saved catalog.
        if (failed) Serial.printf("[BookOrbit] %d of %d libraries incomplete.\n", failed, (int)libs.size());
        return failed < (int)libs.size();
    }

    String initialUrl = buildUrl(serverHost, "/api/v1/opds/catalog");
    return fetchCatalogFromUrl(serverHost, initialUrl, username, password, token, "", outBooks);
}

bool BookOrbitClient::downloadFile(const String& serverHost, const String& fileUrl, const String& destSdPath, const String& username, const String& password, const String& token, void (*onProgress)(int percent), bool (*checkCancel)()) {
    if (!isConnected()) return false;

    // Ensure Wi-Fi radio is running at maximum speed without modem sleep
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    HTTPClient http;
    String cleanUrl = fileUrl;
    cleanUrl.replace("&amp;", "&");
    String fullUrl = cleanUrl.startsWith("http") ? cleanUrl : buildUrl(serverHost, cleanUrl);
    Serial.printf("[BookOrbit] Downloading file from: %s -> %s\n", fullUrl.c_str(), destSdPath.c_str());

    http.begin(fullUrl);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);
    http.addHeader("Connection", "close");
    http.addHeader("Accept-Encoding", "identity");

    // Book Orbit OPDS acquisition and feed endpoints require HTTP Basic Auth:
    // REST API endpoints require Bearer JWT token.
    bool preferBasic = (cleanUrl.indexOf("/opds") >= 0);
    bool hasBasic = (username.length() > 0 && password.length() > 0);
    bool hasToken = (token.length() > 0);

    String authHeader = "";
    if (preferBasic && hasBasic) {
        authHeader = "Basic " + base64::encode(username + ":" + password);
    } else if (hasToken) {
        authHeader = "Bearer " + token;
    } else if (hasBasic) {
        authHeader = "Basic " + base64::encode(username + ":" + password);
    }

    if (authHeader.length() > 0) {
        http.addHeader("Authorization", authHeader);
    }

    uint32_t tStart = millis();
    int code = http.GET();

    // If unauthorized or forbidden, retry with alternate authentication scheme
    if ((code == 401 || code == 403) && hasBasic && hasToken) {
        http.end();
        String altHeader = (authHeader.startsWith("Basic")) ? ("Bearer " + token) : ("Basic " + base64::encode(username + ":" + password));
        Serial.printf("[BookOrbit] HTTP %d with %s auth, retrying with %s...\n", 
                      code, authHeader.startsWith("Basic") ? "Basic" : "Bearer", 
                      altHeader.startsWith("Basic") ? "Basic" : "Bearer");
        http.begin(fullUrl);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(30000);
        http.addHeader("Connection", "close");
        http.addHeader("Accept-Encoding", "identity");
        http.addHeader("Authorization", altHeader);
        code = http.GET();
    }

    if (code < 200 || code >= 300) {
        Serial.printf("[BookOrbit] Download failed with HTTP %d\n", code);
        http.end();
        return false;
    }

    int totalBytes = http.getSize();
    Serial.printf("[BookOrbit] Target size (%d bytes) -> %s\n", totalBytes, destSdPath.c_str());

    Storage.ensureDirectories();

    // Written beside the target and swapped in only when complete: a reset or a
    // cut connection never leaves a full-size file of garbage that passes for
    // the book, and a failed new download keeps the copy already on the card.
    const String partPath = destSdPath + ".part";
    if (Storage.exists(partPath.c_str())) Storage.remove(partPath.c_str());
    FsFile file = Storage.open(partPath.c_str(), O_WRITE | O_CREAT | O_TRUNC);
    if (!file) {
        Serial.printf("[BookOrbit] Cannot open %s for write on SD!\n", partPath.c_str());
        http.end();
        return false;
    }

    // Preallocate contiguous blocks on SD card if size is known (huge speedup in FAT32)
    if (totalBytes > 0) {
        if (file.preAllocate(totalBytes)) {
            Serial.printf("[BookOrbit] Pre-allocated %d contiguous bytes on SD card.\n", totalBytes);
        }
    }

    // Buffered SD writer fed by HTTPClient::writeToStream(), which decodes chunked
    // transfer encoding and stops at the end of the body (reading the raw socket
    // used to save chunk headers into the file and wait 15 s for keep-alive
    // connections to time out).
    size_t bufSize = 16384;
    uint8_t* buffer = nullptr;
    while (bufSize >= 1024 && !(buffer = (uint8_t*)malloc(bufSize))) bufSize /= 2;
    if (!buffer) {
        Serial.println("[BookOrbit] Failed to allocate download buffer!");
        file.close();
        Storage.remove(partPath.c_str());
        http.end();
        return false;
    }
    FileDownloadSink sink(file, buffer, bufSize, totalBytes, onProgress, checkCancel);
    const int result = http.writeToStream(&sink);
    const bool flushed = sink.finish();
    file.flush();
    file.close();
    free(buffer);
    http.end();

    const size_t downloaded = sink.bytes();
    uint32_t elapsed = millis() - tStart;
    float speedKBps = (elapsed > 0) ? ((float)downloaded / (float)elapsed * 1000.0f / 1024.0f) : 0.0f;
    Serial.printf("[BookOrbit] Download finished: %u bytes in %lu ms (%.1f KB/s), result %d%s.\n",
                  (unsigned)downloaded, elapsed, speedKBps, result, sink.cancelled() ? " (cancelled)" : "");

    bool success = result >= 0 && flushed && !sink.cancelled() && downloaded > 0 &&
                   (totalBytes <= 0 || downloaded >= (size_t)totalBytes);
    if (success && !Storage.replaceFile(partPath.c_str(), destSdPath.c_str())) {
        Serial.printf("[BookOrbit] Cannot move %s into place\n", partPath.c_str());
        success = false;
    }
    if (!success) {
        Storage.remove(partPath.c_str());
        Serial.printf("[BookOrbit] Incomplete download removed from SD: %s (%u / %d bytes)\n",
                      partPath.c_str(), (unsigned)downloaded, totalBytes);
    } else if (onProgress) {
        onProgress(100);
    }
    return success;
}

bool BookOrbitClient::downloadBook(const String& serverHost, const String& downloadUrl, const String& destSdPath, const String& username, const String& password, const String& token, void (*onProgress)(int percent), bool (*checkCancel)()) {
    return downloadFile(serverHost, downloadUrl, destSdPath, username, password, token, onProgress, checkCancel);
}

bool BookOrbitClient::downloadCover(const String& serverHost, const String& coverUrl, const String& destSdPath, const String& username, const String& password, const String& token, bool (*checkCancel)()) {
    if (coverUrl.length() == 0) return false;
    return downloadFile(serverHost, coverUrl, destSdPath, username, password, token, nullptr, checkCancel);
}

bool BookOrbitClient::fetchBookProgress(const String& serverHost, const String& username, const String& password, String& token, int bookId, int fileId, ServerProgress& out) {
    if (!isConnected() || bookId <= 0) return false;

    // Login if token is missing
    if (token.length() == 0 && username.length() > 0 && password.length() > 0) {
        login(serverHost, username, password, token);
    }
    if (token.length() == 0) return false;

    HTTPClient http;
    String url = buildUrl(serverHost, "/api/v1/books/" + String(bookId) + "/progress");
    http.begin(url);
    http.setTimeout(10000);
    http.addHeader("Connection", "close");
    http.addHeader("Authorization", "Bearer " + token);

    collectDate(http);
    int code = http.GET();
    takeServerTime(http);
    // If token expired (401), try to re-login once and retry
    if (code == 401 && username.length() > 0 && password.length() > 0) {
        http.end();
        Serial.println("[BookOrbit] Progress fetch returned 401, refreshing token...");
        if (login(serverHost, username, password, token)) {
            http.begin(url);
            http.setTimeout(10000);
            http.addHeader("Connection", "close");
            http.addHeader("Authorization", "Bearer " + token);
            collectDate(http);
            code = http.GET();
            takeServerTime(http);
        }
    }

    if (code != 200) {
        Serial.printf("[BookOrbit] Progress fetch (%s) failed HTTP %d\n", url.c_str(), code);
        http.end();
        return false;
    }

    String json = http.getString();
    http.end();

    BookOrbitProtocol::Progress parsed;
    if (!BookOrbitProtocol::parseBookProgress(json.c_str(), parsed, fileId)) {
        Serial.printf("[BookOrbit] Book %d progress: malformed response: %s\n", bookId, json.c_str());
        return false;
    }
    out = ServerProgress();
    if (parsed.exists) {
        out.exists = true;
        out.percent = parsed.percent;
        out.page = parsed.page;
        out.cfi = parsed.cfi.c_str();
        out.updatedAt = parsed.updatedAt;
        Serial.printf("[BookOrbit] Book %d file %d progress from server: %.1f%% (page %d, cfi='%s', updated=%lu) raw: %s\n",
                      bookId, fileId, out.percent * 100.0f, out.page, out.cfi.c_str(), (unsigned long)out.updatedAt, json.c_str());
    } else {
        Serial.printf("[BookOrbit] Book %d file %d has no progress on the server.\n", bookId, fileId);
    }
    return true;
}

bool BookOrbitClient::updateBookProgress(const String& serverHost, const String& username, const String& password, String& token, int fileId, float percent, int page, const String& cfi, uint32_t* outUpdatedAt) {
    if (!isConnected() || fileId <= 0) return false;

    if (token.length() == 0 && username.length() > 0 && password.length() > 0) {
        login(serverHost, username, password, token);
    }
    if (token.length() == 0) return false;

    HTTPClient http;
    String url = buildUrl(serverHost, "/api/v1/reading-progress");
    http.begin(url);
    http.setTimeout(10000);
    http.addHeader("Connection", "close");
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + token);

    const String jsonPayload = BookOrbitProtocol::buildProgressWrite(fileId, percent, page, cfi.c_str()).c_str();

    collectDate(http);
    int code = http.PUT(jsonPayload);
    takeServerTime(http);
    // If token expired (401), try to re-login once and retry
    if (code == 401 && username.length() > 0 && password.length() > 0) {
        http.end();
        Serial.println("[BookOrbit] updateBookProgress returned 401, refreshing token...");
        if (login(serverHost, username, password, token)) {
            http.begin(url);
            http.setTimeout(10000);
            http.addHeader("Connection", "close");
            http.addHeader("Content-Type", "application/json");
            http.addHeader("Authorization", "Bearer " + token);
            collectDate(http);
            code = http.PUT(jsonPayload);
            takeServerTime(http);
        }
    }

    String respPayload = "";
    if (code >= 200 && code < 300) {
        respPayload = http.getString();
    }
    http.end();

    if (outUpdatedAt && respPayload.length() > 0) {
        *outUpdatedAt = BookOrbitProtocol::parseProgressWriteUpdatedAt(respPayload.c_str());
    }

    Serial.printf("[BookOrbit] updateBookProgress %s -> HTTP %d %s\n", jsonPayload.c_str(), code, respPayload.c_str());
    return code >= 200 && code < 300;   // 201 / 204 are a success too: else the record stays dirty for good
}


int BookOrbitClient::authedRequest(const String& serverHost, const String& username, const String& password,
                                   String& token, const char* method, const String& path, const String& body,
                                   String* response) {
    if (!isConnected()) return -1;
    if (token.length() == 0 && username.length() > 0 && password.length() > 0) login(serverHost, username, password, token);
    if (token.length() == 0) return 401;
    const String url = buildUrl(serverHost, path);
    int code = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        HTTPClient http;
        http.begin(url);
        http.setTimeout(10000);
        http.addHeader("Connection", "close");
        http.addHeader("Authorization", "Bearer " + token);
        if (body.length() > 0) http.addHeader("Content-Type", "application/json");
        collectDate(http);
        code = http.sendRequest(method, body);
        takeServerTime(http);
        if (code >= 200 && code < 300 && response) *response = http.getString();
        http.end();
        if (code != 401 || attempt > 0 || username.length() == 0 || password.length() == 0) break;
        Serial.printf("[BookOrbit] %s %s returned 401, refreshing token...\n", method, path.c_str());
        if (!login(serverHost, username, password, token)) break;
    }
    if (code < 200 || code >= 300) Serial.printf("[BookOrbit] %s %s failed: HTTP %d\n", method, path.c_str(), code);
    return code;
}

bool BookOrbitClient::fetchBookmarks(const String& serverHost, const String& username, const String& password,
                                     String& token, int bookId, int fileId,
                                     std::vector<BookOrbitProtocol::Bookmark>& out) {
    out.clear();
    if (bookId <= 0) return false;
    String path = "/api/v1/books/" + String(bookId) + "/bookmarks";
    if (fileId > 0) path += "?readerFileId=" + String(fileId);
    String json;
    const int code = authedRequest(serverHost, username, password, token, "GET", path, "", &json);
    if (code != 200) return false;
    if (!BookOrbitProtocol::parseBookmarks(json.c_str(), out)) {
        Serial.printf("[BookOrbit] Book %d bookmarks: malformed response\n", bookId);
        return false;
    }
    return true;
}

int BookOrbitClient::createBookmark(const String& serverHost, const String& username, const String& password,
                                    String& token, int bookId, int fileId, const String& cfi, const String& title) {
    if (bookId <= 0 || cfi.length() == 0) return 0;
    const String body = BookOrbitProtocol::buildBookmarkCreate(fileId, cfi.c_str(), title.c_str()).c_str();
    String json;
    const int code = authedRequest(serverHost, username, password, token, "POST",
                                   "/api/v1/books/" + String(bookId) + "/bookmarks", body, &json);
    if (code < 200 || code >= 300) return 0;
    return BookOrbitProtocol::parseCreatedBookmarkId(json.c_str());
}

bool BookOrbitClient::deleteBookmark(const String& serverHost, const String& username, const String& password,
                                     String& token, int bookId, int bookmarkId) {
    if (bookId <= 0 || bookmarkId <= 0) return false;
    const int code = authedRequest(serverHost, username, password, token, "DELETE",
                                   "/api/v1/books/" + String(bookId) + "/bookmarks/" + String(bookmarkId), "", nullptr);
    return (code >= 200 && code < 300) || code == 404;   // 404: already gone
}
