#include "sd_convert.h"

#include "../hal/storage.h"
#include "../reader/engine/sd_book_storage.h"
#include "fb2_to_epub.h"
#include "mobi_to_epub.h"
#include "txt_to_epub.h"

namespace convert {

namespace {

class SdIn : public InFile {
public:
    bool open(const String& p) {
        _f = Storage.open(p.c_str(), O_RDONLY);
        return (bool)_f;
    }
    ~SdIn() override {
        if (_f) _f.close();
    }
    bool rewind() override { return _f.seek(0); }
    bool seek(uint64_t pos) override { return _f.seek((uint32_t)pos); }
    int32_t read(void* dst, uint32_t len) override {
        const uint32_t t = micros();
        const int32_t n = _f.read((uint8_t*)dst, len);
        us += micros() - t;
        return n;
    }
    uint64_t size() const override { return const_cast<FsFile&>(_f).size(); }
    uint32_t us = 0;

private:
    FsFile _f;
};

// Buffered: the card is written in whole 512-byte sectors where it can be
// (a write ending mid-sector costs a read-modify-write of that sector).
class SdOut : public OutFile {
public:
    static constexpr uint32_t kBuf = 8192;
    bool open(const String& p) {
        _f = Storage.open(p.c_str(), O_RDWR | O_CREAT | O_TRUNC);
        _buf = (uint8_t*)malloc(kBuf);
        return _f && _buf;
    }
    bool close() {
        if (!_f) return false;
        const bool ok = flush(true);
        _f.close();
        return ok;
    }
    ~SdOut() override {
        if (_f) _f.close();
        free(_buf);
    }
    bool write(const void* d, uint32_t n) override {
        const uint8_t* p = (const uint8_t*)d;
        while (n) {
            const uint32_t k = n < kBuf - _len ? n : kBuf - _len;
            memcpy(_buf + _len, p, k);
            _len += k;
            _pos += k;
            p += k;
            n -= k;
            if (_len == kBuf && !flush(false)) return false;
        }
        return true;
    }
    bool seek(uint32_t pos) override {
        if (!flush(true) || !_f.seek(pos)) return false;
        _pos = pos;
        return true;
    }
    uint32_t tell() override { return _pos; }
    uint32_t us = 0;

private:
    // Writes the buffer, all of it or up to a sector boundary (the rest stays).
    bool flush(bool all) {
        if (!_len) return true;
        const uint32_t start = _pos - _len;
        uint32_t n = _len;
        if (!all) {
            const uint32_t tail = (start + _len) % 512;
            if (tail < _len) n = _len - tail;
        }
        const uint32_t t = micros();
        const bool ok = _f.write(_buf, n) == n;
        us += micros() - t;
        memmove(_buf, _buf + n, _len - n);
        _len -= n;
        return ok;
    }
    FsFile _f;
    uint8_t* _buf = nullptr;
    uint32_t _len = 0;
    uint32_t _pos = 0;
};

struct TimedProgress {
    ProgressFn fn;
    void* user;
    uint32_t us = 0;
    int calls = 0;
    static void call(void* self, int permille) {
        TimedProgress* t = static_cast<TimedProgress*>(self);
        const uint32_t s = micros();
        if (t->fn) t->fn(t->user, permille);
        t->us += micros() - s;
        t->calls++;
    }
};

class SdTemp : public TempFile {
public:
    explicit SdTemp(const String& p) : _path(p) {}
    ~SdTemp() override {
        if (_f) _f.close();
        Storage.remove(_path.c_str());
    }
    bool beginWrite() override {
        if (_f) _f.close();
        _f = Storage.open(_path.c_str(), O_RDWR | O_CREAT | O_TRUNC);
        return (bool)_f;
    }
    bool write(const void* d, uint32_t n) override { return _f && _f.write((const uint8_t*)d, n) == n; }
    bool beginRead() override { return _f && _f.seek(0); }
    int32_t read(void* dst, uint32_t len) override { return _f ? _f.read((uint8_t*)dst, len) : -1; }

private:
    String _path;
    FsFile _f;
};

String stampOf(const String& path) {
    FsFile f = Storage.open(path.c_str(), O_RDONLY);
    if (!f) return "";
    const uint64_t size = f.size();
    f.close();
    char s[48];
    String lower = path;
    lower.toLowerCase();
    const int version = lower.endsWith(".fb2") ? kFb2ConverterVersion
                        : lower.endsWith(".azw3") || lower.endsWith(".azw") ? kKf8ConverterVersion
                                                                             : kConverterVersion;
    snprintf(s, sizeof(s), "v%d %llu", version, (unsigned long long)size);
    return s;
}

String readSmall(const String& path) {
    FsFile f = Storage.open(path.c_str(), O_RDONLY);
    if (!f) return "";
    char buf[96];
    const int n = f.read((uint8_t*)buf, sizeof(buf) - 1);
    f.close();
    buf[n > 0 ? n : 0] = 0;
    return buf;
}

}  // namespace

bool needsConversion(const String& path) {
    String l = path;
    l.toLowerCase();
    return l.endsWith(".fb2") || l.endsWith(".txt") || l.endsWith(".mobi") || l.endsWith(".azw") || l.endsWith(".azw3") || l.endsWith(".prc");
}

bool ensureConverted(const String& path, const String& cacheDir, String& epubPath, ProgressFn progress, void* user,
                     bool* noMemory, ConvertProblem* why) {
    if (noMemory) *noMemory = false;
    if (why) *why = ConvertProblem::None;
    epubPath = cacheDir + "/book.epub";
    const String stampPath = cacheDir + "/source.txt";
    const String stamp = stampOf(path);
    if (stamp.length() && readSmall(stampPath) == stamp && Storage.exists(epubPath.c_str())) return true;

    // A new or changed book (or converter): its old index and pages go too.
    Storage.mkdir("/cache");
    Storage.mkdir(cacheDir.c_str());
    {
        SdCacheStorage dir;
        dir.setDir(cacheDir);
        dir.clear();
    }
    const uint32_t t0 = millis();
    SdIn in;
    if (!in.open(path)) return false;
    const String tmpEpub = epubPath + ".tmp";
    SdOut out;
    if (!out.open(tmpEpub)) return false;
    SdTemp toc(cacheDir + "/toc.tmp");
    ConvertStats stats;
    TimedProgress timed{progress, user};
    String lower = path;
    lower.toLowerCase();
    ConvertResult r;
    if (lower.endsWith(".fb2")) {
        r = fb2ToEpub(in, out, toc, &TimedProgress::call, &timed, &stats);
    } else if (lower.endsWith(".txt")) {
        // a text file has no title of its own: its file name
        String stem = path.substring(path.lastIndexOf('/') + 1);
        stem = stem.substring(0, stem.length() - 4);
        r = txtToEpub(in, out, stem.c_str(), &TimedProgress::call, &timed, &stats);
    } else {
        r = mobiToEpub(in, out, toc, &TimedProgress::call, &timed, &stats);
    }
    const bool closed = out.close();
    if (r != ConvertResult::Ok || !closed || !Storage.replaceFile(tmpEpub.c_str(), epubPath.c_str())) {
        Serial.printf("[Convert] %s: %s %s\n", path.c_str(), resultName(r), stats.error.c_str());
        Storage.remove(tmpEpub.c_str());
        if (noMemory) *noMemory = r == ConvertResult::NoMemory;
        if (why) {
            *why = r == ConvertResult::NoMemory      ? ConvertProblem::NoMemory
                   : r == ConvertResult::Unsupported ? (stats.error.find("DRM") != std::string::npos ? ConvertProblem::Drm
                                                                                                  : ConvertProblem::Unsupported)
                                                     : ConvertProblem::Broken;
        }
        return false;
    }
    // FB2: where its files stand in the web reader's layout (CFIs; core/cfi_map.h).
    const String mapPath = cacheDir + "/" + kCfiMapName;
    if (stats.cfiMap.empty()) {
        if (Storage.exists(mapPath.c_str())) Storage.remove(mapPath.c_str());
    } else if (FsFile m = Storage.open(mapPath.c_str(), O_RDWR | O_CREAT | O_TRUNC)) {
        m.write((const uint8_t*)stats.cfiMap.data(), stats.cfiMap.size());
        m.close();
    }
    FsFile s = Storage.open(stampPath.c_str(), O_RDWR | O_CREAT | O_TRUNC);
    if (s) {
        s.write((const uint8_t*)stamp.c_str(), stamp.length());
        s.close();
    }
    Serial.printf("[Convert] %s -> EPUB in %lu ms (card reads %lu ms, writes %lu ms, progress window %lu ms): "
                  "%u chapters, %u images, %u contents entries, %u ids%s%s\n", path.c_str(),
                  (unsigned long)(millis() - t0), (unsigned long)(in.us / 1000), (unsigned long)(out.us / 1000),
                  (unsigned long)(timed.us / 1000), (unsigned)stats.chapters, (unsigned)stats.images,
                  (unsigned)stats.tocEntries, (unsigned)stats.ids, lower.endsWith(".txt") ? "; " : "",
                  lower.endsWith(".txt") ? stats.error.c_str() : "");
    return true;
}

}  // namespace convert
