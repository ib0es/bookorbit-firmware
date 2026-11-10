#include "sd_convert.h"

#include "../hal/storage.h"
#include "../reader/engine/sd_book_storage.h"
#include "fb2_to_epub.h"

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
    int32_t read(void* dst, uint32_t len) override { return _f.read((uint8_t*)dst, len); }
    uint64_t size() const override { return const_cast<FsFile&>(_f).size(); }

private:
    FsFile _f;
};

class SdOut : public OutFile {
public:
    bool open(const String& p) {
        _f = Storage.open(p.c_str(), O_RDWR | O_CREAT | O_TRUNC);
        return (bool)_f;
    }
    bool close() {
        if (!_f) return false;
        _f.close();
        return true;
    }
    ~SdOut() override {
        if (_f) _f.close();
    }
    bool write(const void* d, uint32_t n) override { return _f.write((const uint8_t*)d, n) == n; }
    bool seek(uint32_t pos) override { return _f.seek(pos); }
    uint32_t tell() override { return (uint32_t)_f.position(); }

private:
    FsFile _f;
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
    snprintf(s, sizeof(s), "v%d %llu", kConverterVersion, (unsigned long long)size);
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
    return l.endsWith(".fb2");
}

bool ensureConverted(const String& path, const String& cacheDir, String& epubPath, ProgressFn progress, void* user,
                     bool* noMemory) {
    if (noMemory) *noMemory = false;
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
    const ConvertResult r = fb2ToEpub(in, out, toc, progress, user, &stats);
    const bool closed = out.close();
    if (r != ConvertResult::Ok || !closed || !Storage.replaceFile(tmpEpub.c_str(), epubPath.c_str())) {
        Serial.printf("[Convert] %s: %s %s\n", path.c_str(), resultName(r), stats.error.c_str());
        Storage.remove(tmpEpub.c_str());
        if (noMemory) *noMemory = r == ConvertResult::NoMemory;
        return false;
    }
    FsFile s = Storage.open(stampPath.c_str(), O_RDWR | O_CREAT | O_TRUNC);
    if (s) {
        s.write((const uint8_t*)stamp.c_str(), stamp.length());
        s.close();
    }
    Serial.printf("[Convert] %s -> EPUB in %lu ms: %u chapters, %u images, %u contents entries, %u ids\n", path.c_str(),
                  (unsigned long)(millis() - t0), (unsigned)stats.chapters, (unsigned)stats.images,
                  (unsigned)stats.tocEntries, (unsigned)stats.ids);
    return true;
}

}  // namespace convert
