#pragma once
// Test stub (test/sim): the ESP32 WebServer, so the REAL src/net/web_config.cpp
// (every handler of the portal) runs in the simulator.
//
// The browser is a file: <card>/portal_requests.json, a list of requests that handleClient()
// serves one by one, the way the portal's loop calls it:
//   [{"method":"POST", "uri":"/api/books/delete?pin=777777", "form":{"name":"A.epub"},
//     "headers":{"X-PIN":"777777"},
//     "files":[{"name":"Book.epub", "size":3000, "hex":"00010000", "fill":"x"}]}, ...]
// "form" are the body's fields, "headers" the request's; a "files" entry is an upload: the
// handler sees START, the WRITE chunks of 1436 bytes, END, then the request's own handler
// answers (as the ESP32 WebServer does). A file is "size" bytes: "hex" first, then "fill".
// Every answer is printed as one line:  [PortalResp] {"i":0,"code":200,"type":"...","body":"..."}
// (and a streamed page is joined). When the list is used up nothing more happens: it
// ends with POST /api/close, which ends the portal's loop.

#include "Arduino.h"
#include "platform/hal/storage.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

enum HTTPMethod { HTTP_ANY = 0, HTTP_GET = 1, HTTP_POST = 3 };
enum HTTPUploadStatus { UPLOAD_FILE_START, UPLOAD_FILE_WRITE, UPLOAD_FILE_END, UPLOAD_FILE_ABORTED };
constexpr size_t CONTENT_LENGTH_UNKNOWN = (size_t)-1;

struct HTTPUpload {
    HTTPUploadStatus status = UPLOAD_FILE_START;
    String filename;
    String name;
    String type;
    size_t totalSize = 0;
    size_t currentSize = 0;
    uint8_t* buf = nullptr;
};

class WebServer {
public:
    typedef std::function<void()> THandlerFunction;

    explicit WebServer(int = 80) {}

    void on(const char* uri, THandlerFunction fn) { _routes.push_back({uri, HTTP_ANY, fn, nullptr}); }
    void on(const char* uri, HTTPMethod m, THandlerFunction fn) { _routes.push_back({uri, m, fn, nullptr}); }
    void on(const char* uri, HTTPMethod m, THandlerFunction fn, THandlerFunction upload) {
        _routes.push_back({uri, m, fn, upload});
    }
    void onNotFound(THandlerFunction fn) { _notFound = fn; }
    void begin() { loadScript(); }
    void stop() {}

    void handleClient() {
        if (_next >= _script.size()) return;
        serve(_script[_next], (int)_next);
        _next++;
    }

    bool hasArg(const String& name) const { return _args.count(name.c_str()) > 0; }
    String arg(const String& name) const {
        auto it = _args.find(name.c_str());
        return it == _args.end() ? String() : String(it->second.c_str());
    }
    bool hasHeader(const String& name) const { return _headers.count(name.c_str()) > 0; }
    String header(const String& name) const {
        auto it = _headers.find(name.c_str());
        return it == _headers.end() ? String() : String(it->second.c_str());
    }
    HTTPUpload& upload() { return _upload; }

    void sendHeader(const String& name, const String& value, bool = false) {
        _respHeaders[name.c_str()] = value.c_str();
    }
    void setContentLength(size_t) {}
    void send(int code, const char* type, const String& body) {
        _code = code;
        _type = type ? type : "";
        _body += body.c_str();
        _sent = true;
    }
    void sendContent(const String& chunk) {
        _body += chunk.c_str();
        _sent = true;
    }
    void sendContent(const char* data, size_t length) {
        _body.append(data, length);
        _sent = true;
    }

private:
    struct Route {
        std::string uri;
        HTTPMethod method;
        THandlerFunction handler;
        THandlerFunction upload;
    };

    static std::string hexBytes(const std::string& hex) {
        std::string out;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) out += (char)strtol(hex.substr(i, 2).c_str(), nullptr, 16);
        return out;
    }

    void loadScript() {
        _script.clear();
        _next = 0;
        FsFile f = Storage.open("/portal_requests.json", O_RDONLY);
        if (!f) return;
        std::string text(f.size(), '\0');
        if (!text.empty()) f.read(&text[0], text.size());
        f.close();
        JsonDocument doc;
        if (deserializeJson(doc, text)) {
            printf("[PortalResp] {\"error\":\"portal_requests.json does not parse\"}\n");
            return;
        }
        for (JsonObject r : doc.as<JsonArray>()) {
            Request q;
            q.method = r["method"] | "GET";
            q.uri = r["uri"] | "/";
            for (JsonPair kv : r["form"].as<JsonObject>()) q.form[kv.key().c_str()] = kv.value().as<std::string>();
            for (JsonPair kv : r["headers"].as<JsonObject>()) q.headers[kv.key().c_str()] = kv.value().as<std::string>();
            for (JsonObject file : r["files"].as<JsonArray>()) {
                File up;
                up.name = file["name"] | "";
                up.data = hexBytes(file["hex"] | "");
                const size_t size = file["size"] | (unsigned)up.data.size();
                const std::string fill = file["fill"] | "x";
                while (up.data.size() < size) up.data += fill.empty() ? 'x' : fill[0];
                if (up.data.size() > size) up.data.resize(size);
                q.files.push_back(up);
            }
            _script.push_back(q);
        }
    }

    struct File {
        std::string name;
        std::string data;
    };
    struct Request {
        std::string method;
        std::string uri;
        std::map<std::string, std::string> form;
        std::map<std::string, std::string> headers;
        std::vector<File> files;
    };

    static std::string urlDecode(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '%' && i + 2 < s.size()) {
                out += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            } else if (s[i] == '+') {
                out += ' ';
            } else {
                out += s[i];
            }
        }
        return out;
    }

    static std::string jsonString(const std::string& s) {
        std::string out = "\"";
        char buf[8];
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += (char)c;
            } else if (c < 0x20) {
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
        return out + "\"";
    }

    void serve(const Request& q, int index) {
        std::string path = q.uri;
        _args.clear();
        const size_t qm = path.find('?');
        if (qm != std::string::npos) {
            std::string query = path.substr(qm + 1);
            path = path.substr(0, qm);
            for (size_t at = 0; at <= query.size();) {
                size_t amp = query.find('&', at);
                if (amp == std::string::npos) amp = query.size();
                const std::string pair = query.substr(at, amp - at);
                const size_t eq = pair.find('=');
                if (!pair.empty()) _args[urlDecode(pair.substr(0, eq))] = eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
                at = amp + 1;
            }
        }
        for (const auto& kv : q.form) _args[kv.first] = kv.second;
        _headers = q.headers;
        _respHeaders.clear();
        _body.clear();
        _type.clear();
        _code = 0;
        _sent = false;

        const Route* route = nullptr;
        const HTTPMethod method = q.method == "POST" ? HTTP_POST : HTTP_GET;
        for (const Route& r : _routes) {
            if (r.uri == path && (r.method == HTTP_ANY || r.method == method)) {
                route = &r;
                break;
            }
        }
        if (!route) {
            if (_notFound) _notFound();
        } else {
            if (route->upload && !q.files.empty()) {
                for (const File& file : q.files) {
                    _upload = HTTPUpload();
                    _upload.filename = file.name.c_str();
                    _upload.name = "file";
                    _upload.totalSize = file.data.size();
                    _upload.status = UPLOAD_FILE_START;
                    route->upload();
                    std::string chunk;
                    for (size_t at = 0; at < file.data.size(); at += 1436) {
                        chunk = file.data.substr(at, 1436);
                        _upload.status = UPLOAD_FILE_WRITE;
                        _upload.buf = (uint8_t*)&chunk[0];
                        _upload.currentSize = chunk.size();
                        route->upload();
                    }
                    _upload.status = UPLOAD_FILE_END;
                    _upload.currentSize = 0;
                    _upload.buf = nullptr;
                    route->upload();
                }
            }
            route->handler();
        }
        std::string headers = "{";
        for (const auto& kv : _respHeaders) {
            if (headers.size() > 1) headers += ',';
            headers += jsonString(kv.first) + ":" + jsonString(kv.second);
        }
        headers += "}";
        printf("[PortalResp] {\"i\":%d,\"uri\":%s,\"code\":%d,\"type\":%s,\"headers\":%s,\"body\":%s}\n", index,
               jsonString(q.uri).c_str(), _sent ? (_code ? _code : 200) : 0, jsonString(_type).c_str(), headers.c_str(),
               jsonString(_body).c_str());
        fflush(stdout);
    }

    std::vector<Route> _routes;
    THandlerFunction _notFound;
    std::vector<Request> _script;
    size_t _next = 0;
    std::map<std::string, std::string> _args;
    std::map<std::string, std::string> _headers;
    std::map<std::string, std::string> _respHeaders;
    HTTPUpload _upload;
    int _code = 0;
    std::string _type;
    std::string _body;
    bool _sent = false;
};
