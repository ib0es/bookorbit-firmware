#pragma once
// Test stub (test/sim): in-process HTTP client. Requests are answered by
// FakeNet::handle() (test/sim/fake_bookorbit.cpp) instead of a socket.
#include <map>
#include <string>
#include <vector>
#include "Arduino.h"
#include "WiFi.h"

enum followRedirects_t { HTTPC_DISABLE_FOLLOW_REDIRECTS, HTTPC_STRICT_FOLLOW_REDIRECTS, HTTPC_FORCE_FOLLOW_REDIRECTS };
#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_STREAM_WRITE (-10)

namespace FakeNet {
struct Request {
    std::string method, url, body;
    std::map<std::string, std::string> headers;   // lower-case names
};
struct Response {
    int code = 200;
    std::string body;
    std::map<std::string, std::string> headers;   // e.g. "Date"
};
Response handle(const Request& req);
}

class WiFiClient : public Stream {
public:
    void reset(const std::string& data) { _data = data; _pos = 0; }
    int available() override { return (int)(_data.size() - _pos); }
    int read() override { return _pos < _data.size() ? (uint8_t)_data[_pos++] : -1; }
    int read(uint8_t* buf, size_t n) {
        size_t k = (std::min)(n, _data.size() - _pos);
        memcpy(buf, _data.data() + _pos, k); _pos += k; return (int)k;
    }
    int peek() override { return _pos < _data.size() ? (uint8_t)_data[_pos] : -1; }
    size_t write(uint8_t) override { return 1; }
    void setNoDelay(bool) {}
    void setTimeout(unsigned long) {}
private:
    std::string _data;
    size_t _pos = 0;
};

class HTTPClient {
public:
    bool begin(const String& url) { _req = FakeNet::Request(); _req.url = url.c_str(); _done = false; return true; }
    void end() {}
    void setTimeout(uint32_t) {}
    void setFollowRedirects(followRedirects_t) {}
    void addHeader(const String& name, const String& value) {
        std::string n = name.c_str();
        for (auto& c : n) c = (char)tolower((unsigned char)c);
        _req.headers[n] = value.c_str();
    }
    void collectHeaders(const char* keys[], size_t count) { _collect.assign(keys, keys + count); }
    String header(const char* name) {
        for (const auto& k : _collect) if (k == name) {
            auto it = _resp.headers.find(name);
            return it == _resp.headers.end() ? String("") : String(it->second.c_str());
        }
        return String("");
    }
    int GET() { return send("GET", ""); }
    int POST(const String& body) { return send("POST", body.c_str()); }
    int PUT(const String& body) { return send("PUT", body.c_str()); }
    String getString() { return String(_resp.body.c_str()); }
    int getSize() { return (int)_resp.body.size(); }
    WiFiClient* getStreamPtr() { return &_stream; }
    bool connected() { return _stream.available() > 0; }
    int writeToStream(Stream* s) {
        // Real HTTPClient feeds the body in TCP-sized pieces; do the same.
        const std::string& b = _resp.body;
        for (size_t i = 0; i < b.size(); i += 1436) {
            const size_t n = (std::min<size_t>)(1436, b.size() - i);
            if (s->write((const uint8_t*)b.data() + i, n) != n) return HTTPC_ERROR_STREAM_WRITE;
        }
        return (int)b.size();
    }
    static String errorToString(int code) { return String("error ") + String(code); }
private:
    int send(const char* method, const std::string& body) {
        _req.method = method;
        _req.body = body;
        if (WiFi.status() != WL_CONNECTED) { _resp = FakeNet::Response(); _resp.code = HTTPC_ERROR_CONNECTION_REFUSED; return _resp.code; }
        _resp = FakeNet::handle(_req);
        _stream.reset(_resp.body);
        return _resp.code;
    }
    FakeNet::Request _req;
    FakeNet::Response _resp;
    std::vector<std::string> _collect;
    WiFiClient _stream;
    bool _done = false;
};
