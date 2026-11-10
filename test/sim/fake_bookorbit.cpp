// Fake Book Orbit server for the headless simulator tests, at the HTTP level:
// the real src/net/bookorbit_client.cpp runs against it through
// test/sim/stubs_net/HTTPClient.h, so request building and response parsing
// are tested too.
//
// State lives in <sd root>/server.json, which test scenarios edit between steps
// (the 'S' script command renames server_next.json to server.json):
//   {"progress":[{"fileId":101,"percentage":30,"pageNumber":0,"koreaderProgress":"","cfi":"",
//                 "updatedAt":"NOW-100"}, ...],      percentage 0..100, several records allowed
//    "fail":true,            -> every API request answers HTTP 500
//    "expireToken":true,     -> the next Bearer request answers 401 once ("PUT": the next PUT)
//    "deviceHeap":5000,      -> ESP.getFreeHeap() on the "device" (memory guards)
//    "wifiDown":true,        -> Wi-Fi connections fail
//    "backOnWifi":true,      -> BACK is pressed while Wi-Fi connects (once)
//    "backOn":"/download",   -> BACK is pressed when a request path contains this (once)
//    "failPut":true,         -> progress uploads answer HTTP 500
//    "brokenCatalog":true,   -> OPDS entries without acquisition links
//    "bookmarks":[{"id":1,"bookId":1,"fileId":101,"cfi":"epubcfi(...)","title":"..."}]}
//                            -> web-reader bookmarks (GET/POST/DELETE /api/v1/books/:id/bookmarks)
//    "failDelete":true       -> bookmark deletes answer HTTP 500
//   Any request to the KOSync API answers 404: the firmware must not use it.
//   Legacy shorthand: {"percent":0.3,"page":0,"xpath":"","updatedAt":"NOW-100"} (file 101).
//   "NOW+/-N" = now +/- N seconds; written back as ISO 8601 with milliseconds.
// Uploads update (or add) the record of that fileId with updatedAt = now.
#include "Arduino.h"
#include "HTTPClient.h"
#include "SDCardManager.h"
#include "net/web_config.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

WebConfigManager WebConfig;
WebConfigManager::WebConfigManager() {}
bool WebConfigManager::runPortal(AppConfig&) { return false; }


// ArduinoJson may be built without std::string support: go through a buffer.
static std::string toStdString(const JsonDocument& doc) {
    std::string out(measureJson(doc), '\0');
    serializeJson(doc, &out[0], out.size() + 1);
    return out;
}

#include "InputManager.h"

namespace {

using FakeNet::Request;
using FakeNet::Response;

// Extra formats of a book, besides its EPUB (fileId): each its own file, like
// Book Orbit's OPDS (opds_alice.xml). Listed after the EPUB, FB2 last: the
// firmware once downloaded the last link and saved it as ".epub".
struct Format { int fileId; const char* mime; const char* title; };
struct Book { int id; int fileId; const char* title; const char* author; Format extra[3]; };
// Newest first, like the real server's catalog.
const Book kBooks[] = {
    {3, 103, "Moby Dick", "Herman Melville", {}},
    {2, 102, "The Time Machine", "H. G. Wells", {}},
    {1, 101, "Alice in Wonderland", "Lewis Carroll",
     {{131, "text/plain", "TXT"}, {121, "application/vnd.amazon.ebook", "AZW3"}, {111, "application/x-fictionbook+xml", "FB2"}}},
};
const char* kTxtBody = "Alice was beginning to get very tired of sitting by her sister on the bank.\n";
const char* kFb2Body = "<?xml version=\"1.0\"?><FictionBook><body><p>Alice</p></body></FictionBook>\n";
const char* kUser = nullptr;   // from config.json (any user/password accepted)
int g_tokenSerial = 0;

std::string sdPath(const char* p) { return SDCardManager::getInstance().resolvePath(p); }

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

std::string iso(time_t t) {
    char b[40]; struct tm tm; gmtime_r(&t, &tm);
    strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S.123Z", &tm);
    return b;
}

std::string httpDate(time_t t) {
    char b[40]; struct tm tm; gmtime_r(&t, &tm);
    strftime(b, sizeof(b), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return b;
}

// "NOW-100" -> ISO string; numbers -> ISO; strings kept.
std::string isoValue(JsonVariantConst v) {
    if (v.is<const char*>()) {
        std::string s = v.as<const char*>();
        if (s.rfind("NOW", 0) == 0) return iso(time(nullptr) + atol(s.c_str() + 3));
        return s;
    }
    if (v.is<long>()) return iso((time_t)v.as<long>());
    return "";
}

void loadState(JsonDocument& st) {
    std::string s = readFile(sdPath("/server.json"));
    if (s.empty() || deserializeJson(st, s.data(), s.size())) { st.clear(); st["progress"].to<JsonArray>(); return; }
    if (!st["percent"].isNull()) {   // legacy shorthand
        JsonDocument n;
        JsonObject r = n["progress"].to<JsonArray>().add<JsonObject>();
        r["fileId"] = 101;
        r["percentage"] = st["percent"].as<float>() * 100.0f;
        r["pageNumber"] = st["page"] | 0;
        r["koreaderProgress"] = st["xpath"] | "";
        r["updatedAt"] = isoValue(st["updatedAt"]);
        for (JsonPair kv : st.as<JsonObject>()) {   // keep the event keys
            const char* k = kv.key().c_str();
            if (strcmp(k, "percent") && strcmp(k, "page") && strcmp(k, "xpath") && strcmp(k, "updatedAt")) n[k] = kv.value();
        }
        st = n;
    }
    if (st["progress"].isNull()) st["progress"].to<JsonArray>();
    for (JsonObject r : st["progress"].as<JsonArray>()) r["updatedAt"] = isoValue(r["updatedAt"]);
}

void saveState(const JsonDocument& st) {
    std::string out = toStdString(st);
    std::ofstream(sdPath("/server.json"), std::ios::binary) << out;
}

const Book* bookById(int id) { for (const auto& b : kBooks) if (b.id == id) return &b; return nullptr; }
const Book* bookByFile(int f) {
    for (const auto& b : kBooks) {
        if (b.fileId == f) return &b;
        for (const auto& x : b.extra) if (x.fileId && x.fileId == f) return &b;
    }
    return nullptr;
}
bool fileOfBook(const Book& b, int f) {
    if (b.fileId == f) return true;
    for (const auto& x : b.extra) if (x.fileId && x.fileId == f) return true;
    return false;
}

std::string path(const std::string& url) {
    size_t p = url.find("://");
    p = (p == std::string::npos) ? 0 : url.find('/', p + 3);
    return p == std::string::npos ? "/" : url.substr(p);
}

std::string query(const std::string& p, const char* key) {
    size_t q = p.find(std::string(key) + "=");
    if (q == std::string::npos) return "";
    size_t e = p.find('&', q);
    return p.substr(q + strlen(key) + 1, e == std::string::npos ? std::string::npos : e - q - strlen(key) - 1);
}


std::string opdsFeed(int page) {
    const int size = 2, n = sizeof(kBooks) / sizeof(kBooks[0]);
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"
                    "  <title>Catalog</title>\n  <id>urn:bookorbit:catalog</id>\n";
    if ((page) * size < n) x += "  <link rel=\"next\" href=\"/api/v1/opds/catalog?page=" + std::to_string(page + 1) + "&amp;size=2\"/>\n";
    for (int i = (page - 1) * size; i < n && i < page * size; i++) {
        const Book& b = kBooks[i];
        const std::string id = std::to_string(b.id), f = std::to_string(b.fileId);
        x += "<entry>\n  <title>" + std::string(b.title) + "</title>\n  <id>urn:bookorbit:book:" + id + "</id>\n"
             "  <author><name>" + b.author + "</name></author>\n"
             "  <content type=\"text\">Описание &amp; аннотация</content>\n"
             "  <link rel=\"http://opds-spec.org/image/thumbnail\" href=\"/api/v1/opds/" + id + "/thumbnail\" type=\"image/jpeg\"/>\n"
             "  <link rel=\"http://opds-spec.org/acquisition\" href=\"/api/v1/opds/" + id + "/download?fileId=" + f +
             "&amp;selection=abc\" type=\"application/epub+zip\" title=\"EPUB\"/>\n";
        for (const auto& fm : b.extra) {
            if (!fm.fileId) continue;
            x += "  <link rel=\"http://opds-spec.org/acquisition\" href=\"/api/v1/opds/" + id + "/download?fileId=" +
                 std::to_string(fm.fileId) + "\" type=\"" + fm.mime + "\" title=\"" + fm.title + "\"/>\n";
        }
        x += "</entry>\n";
    }
    return x + "</feed>\n";
}

Response route(const Request& rq, JsonDocument& st, bool& dirty) {
    Response r;
    const std::string p = path(rq.url);
    auto hdr = [&](const char* k) { auto it = rq.headers.find(k); return it == rq.headers.end() ? std::string() : it->second; };
    const std::string auth = hdr("authorization");

    if (p == "/api/v1/health") { r.body = "{\"ok\":true}"; return r; }
    if (st["fail"] | false) { r.code = 500; r.body = "{\"error\":\"down\"}"; return r; }

    if (p == "/api/v1/auth/login" && rq.method == "POST") {
        r.body = "{\"accessToken\":\"tok-" + std::to_string(++g_tokenSerial) + "\",\"user\":{}}";
        return r;
    }
    const bool bearer = auth.rfind("Bearer tok-", 0) == 0;
    // "expireToken": true -> next Bearer request, "PUT" -> next Bearer PUT.
    const bool expire = st["expireToken"].is<const char*>() ? rq.method == (st["expireToken"] | "") : (st["expireToken"] | false);
    if (bearer && expire) {
        st.remove("expireToken"); dirty = true;
        r.code = 401; r.body = "{\"error\":\"expired\"}"; return r;
    }

    if (p.rfind("/api/v1/opds/libraries", 0) == 0) {
        if (auth.rfind("Basic ", 0) != 0 && !bearer) { r.code = 401; return r; }
        r.code = 404;
        return r;
    }

    if (p.rfind("/api/v1/opds/catalog", 0) == 0) {
        if (auth.rfind("Basic ", 0) != 0 && !bearer) { r.code = 401; return r; }
        int page = atoi(query(p, "page").c_str()); if (page < 1) page = 1;
        r.body = opdsFeed(page);
        if (st["brokenCatalog"] | false) {   // entries the parser cannot use
            size_t q;
            while ((q = r.body.find("rel=\"http://opds-spec.org/acquisition\"")) != std::string::npos)
                r.body.replace(q, 38, "rel=\"alternate\"");
            while ((q = r.body.find("application/epub+zip")) != std::string::npos) r.body.replace(q, 20, "text/html");
            std::string out;
            for (size_t i = 0; i < r.body.size(); ) {   // also drop the hrefs with "download"
                size_t d = r.body.find("/download", i);
                if (d == std::string::npos) { out += r.body.substr(i); break; }
                out += r.body.substr(i, d - i) + "/x"; i = d + 9;
            }
            r.body = out;
        }
        return r;
    }
    if (p.find("/thumbnail") != std::string::npos || p.find("/covers/") != std::string::npos) {
        r.body = readFile(sdPath("/covers/Pride and Prejudice.jpg")); return r;
    }
    if (p.find("/download") != std::string::npos) {
        std::string f = query(p, "fileId");
        if (f.empty()) f = p.substr(p.rfind('/') + 1);   // /api/v1/download/102
        const int fid = atoi(f.c_str());
        const Book* b = bookByFile(fid);
        if (!b) { r.code = 404; return r; }
        r.body = readFile(sdPath("/books/Pride and Prejudice.epub"));
        for (const auto& x : b->extra) {
            if (x.fileId == fid) r.body = strcmp(x.title, "TXT") == 0 ? kTxtBody : kFb2Body;
        }
        return r;
    }

    if (!bearer) { r.code = 401; r.body = "{\"error\":\"no token\"}"; return r; }

    int bookId = 0;
    if (sscanf(p.c_str(), "/api/v1/books/%d/progress", &bookId) == 1 && p.find("/progress") != std::string::npos &&
        rq.method == "GET") {
        const Book* b = bookById(bookId);
        if (!b) { r.code = 404; return r; }
        JsonDocument out;   // a bare array with web-reader fields, like the real server
        JsonArray items = out.to<JsonArray>();
        for (JsonObject rec : st["progress"].as<JsonArray>()) {   // every file (format) of the book
            if (!fileOfBook(*b, rec["fileId"] | 0)) continue;
            JsonObject it = items.add<JsonObject>();
            it["fileId"] = rec["fileId"];
            it["positionSeconds"] = nullptr;
            it["percentage"] = rec["percentage"];
            if ((rec["pageNumber"] | 0) > 0) it["pageNumber"] = rec["pageNumber"];
            const char* k = rec["koreaderProgress"] | "";
            if (*k) it["koreaderProgress"] = k; else it["koreaderProgress"] = nullptr;
            const char* c = rec["cfi"] | "";
            if (*c) it["cfi"] = c; else it["cfi"] = nullptr;
            it["updatedAt"] = rec["updatedAt"];
        }
        r.body = toStdString(out);
        return r;
    }
    int markId = 0;
    if (sscanf(p.c_str(), "/api/v1/books/%d/bookmarks/%d", &bookId, &markId) == 2 && rq.method == "DELETE") {
        if (st["failDelete"] | false) { r.code = 500; return r; }
        JsonArray marks = st["bookmarks"].as<JsonArray>();
        for (size_t i = 0; i < marks.size(); i++) {
            if ((marks[i]["id"] | 0) == markId && (marks[i]["bookId"] | 0) == bookId) {
                marks.remove(i);
                dirty = true;
                r.code = 204;
                return r;
            }
        }
        r.code = 404;
        return r;
    }
    if (sscanf(p.c_str(), "/api/v1/books/%d/bookmarks", &bookId) == 1 && p.find("/bookmarks/") == std::string::npos) {
        if (!bookById(bookId)) { r.code = 404; return r; }
        if (st["bookmarks"].isNull()) st["bookmarks"].to<JsonArray>();
        JsonArray marks = st["bookmarks"].as<JsonArray>();
        if (rq.method == "GET") {
            const int reader = atoi(query(p, "readerFileId").c_str());
            JsonDocument out;   // a bare array, like the real server
            JsonArray items = out.to<JsonArray>();
            for (JsonObject m : marks) {
                if ((m["bookId"] | 0) != bookId) continue;
                JsonObject it = items.add<JsonObject>();
                it["id"] = m["id"];
                it["bookId"] = bookId;
                // Like the server: a CFI made in another file is converted; here it cannot be.
                const int f = m["fileId"] | 0;
                if (reader > 0 && f > 0 && f != reader) it["cfi"] = nullptr; else it["cfi"] = m["cfi"];
                it["title"] = m["title"];
                it["positionSeconds"] = nullptr;
                it["note"] = nullptr;
            }
            r.body = toStdString(out);
            return r;
        }
        if (rq.method == "POST") {
            JsonDocument in;
            if (deserializeJson(in, rq.body.data(), rq.body.size())) { r.code = 400; return r; }
            for (JsonPair kv : in.as<JsonObject>()) {   // the DTO rejects unknown fields
                const char* k = kv.key().c_str();
                if (strcmp(k, "bookFileId") && strcmp(k, "cfi") && strcmp(k, "title")) { r.code = 400; return r; }
            }
            const char* cfi = in["cfi"] | "";
            const char* title = in["title"] | "";
            if (!*cfi || !*title) { r.code = 400; return r; }
            JsonObject m;
            for (JsonObject e : marks) if ((e["bookId"] | 0) == bookId && strcmp(e["cfi"] | "", cfi) == 0) m = e;
            if (m.isNull()) {   // the server keeps one bookmark per place
                int next = 1;
                for (JsonObject e : marks) next = std::max(next, (e["id"] | 0) + 1);
                m = marks.add<JsonObject>();
                m["id"] = next;
                m["bookId"] = bookId;
                m["fileId"] = in["bookFileId"] | 0;
                m["cfi"] = cfi;
                m["title"] = title;
                dirty = true;
                r.code = 201;
            }
            JsonDocument out;
            out["id"] = m["id"];
            out["bookId"] = bookId;
            out["cfi"] = m["cfi"];
            out["title"] = m["title"];
            r.body = toStdString(out);
            return r;
        }
    }
    if (p == "/api/v1/reading-progress" && rq.method == "PUT" && (st["failPut"] | false)) {
        r.code = 500; r.body = "{\"error\":\"boom\"}"; return r;
    }
    if (p == "/api/v1/reading-progress" && rq.method == "PUT") {
        JsonDocument in;
        if (deserializeJson(in, rq.body.data(), rq.body.size())) { r.code = 400; return r; }
        JsonDocument out;
        JsonArray results = out["results"].to<JsonArray>();
        for (JsonObject w : in["writes"].as<JsonArray>()) {
            const int fileId = w["fileId"] | 0;
            if (!bookByFile(fileId)) { r.code = 404; return r; }
            JsonObject rec;
            for (JsonObject e : st["progress"].as<JsonArray>()) if ((e["fileId"] | 0) == fileId) rec = e;
            if (rec.isNull()) { rec = st["progress"].as<JsonArray>().add<JsonObject>(); rec["fileId"] = fileId; }
            // Like Book Orbit: a write replaces the whole position (fields left out are cleared),
            // and unknown fields are rejected.
            for (JsonPair kv : w) {
                const char* k = kv.key().c_str();
                if (strcmp(k, "fileId") && strcmp(k, "percentage") && strcmp(k, "text") && strcmp(k, "koreader")) {
                    r.code = 400;
                    r.body = std::string("{\"message\":[\"writes.0.property ") + k + " should not exist\"]}";
                    return r;
                }
            }
            rec["percentage"] = w["percentage"];
            rec["pageNumber"] = w["text"]["page"] | 0;
            rec["cfi"] = w["text"]["cfi"] | "";
            rec["koreaderProgress"] = w["koreader"] | "";
            rec["updatedAt"] = iso(time(nullptr));
            JsonObject res = results.add<JsonObject>();
            res["fileId"] = fileId;
            res["progress"]["percentage"] = rec["percentage"];
            res["progress"]["updatedAt"] = rec["updatedAt"];
            dirty = true;
        }
        r.body = toStdString(out);
        return r;
    }
    r.code = 404;
    return r;
}

}  // namespace

namespace {
uint32_t g_backReleaseAt = 0;

void pressBack() {
    InputManager::simPress(InputManager::BTN_BACK);
    g_backReleaseAt = millis() + 400;
    printf("[FakeHttp] *** BACK pressed\n");
}
}  // namespace

namespace FakeNet {

// Called from the headless driver's simPumpEvents(): releases an injected BACK.
void pump() {
    if (g_backReleaseAt && millis() >= g_backReleaseAt) {
        InputManager::simRelease(InputManager::BTN_BACK);
        g_backReleaseAt = 0;
    }
}

bool wifiBegin() {
    JsonDocument st;
    loadState(st);
    if (st["backOnWifi"] | false) {
        st.remove("backOnWifi");
        saveState(st);
        pressBack();
    }
    const bool up = !(st["wifiDown"] | false);
    printf("[FakeHttp] Wi-Fi %s\n", up ? "connected" : "DOWN");
    return up;
}

Response handle(const Request& rq) {
    JsonDocument st;
    loadState(st);
    bool dirty = false;
    const char* backOn = st["backOn"] | "";
    if (*backOn && path(rq.url).find(backOn) != std::string::npos) {
        st.remove("backOn");
        dirty = true;
        pressBack();
    }
    g_simFreeHeap = st["deviceHeap"] | (184u * 1024u);
    Response r = route(rq, st, dirty);
    if (dirty) saveState(st);
    r.headers["Date"] = httpDate(time(nullptr));
    printf("[FakeHttp] %s %s -> %d %s\n", rq.method.c_str(), path(rq.url).c_str(), r.code,
           r.body.size() < 400 && r.body.find('<') == std::string::npos ? r.body.c_str() : "<...>");
    return r;
}
}  // namespace FakeNet
