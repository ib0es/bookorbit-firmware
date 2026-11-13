#include "cover_extract.h"

#include "../core/book_formats.h"
#include "../platform/hal/storage.h"
#include "../platform/system.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace convert {

namespace {

uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int8_t b64Val(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return (int8_t)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int8_t)(26 + c - 'a');
    if (c >= '0' && c <= '9') return (int8_t)(52 + c - '0');
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool tagStartsWith(const std::string& tag, const char* name) {
    const size_t len = strlen(name);
    if (tag.size() < len || tag.compare(0, len, name) != 0) return false;
    if (tag.size() == len) return true;
    const char next = tag[len];
    return isWs(next) || next == '/';
}

std::string findAttr(const std::string& tag, const char* name, bool allowNsPrefix) {
    const size_t nlen = strlen(name);
    for (size_t at = 0; (at = tag.find(name, at)) != std::string::npos; at++) {
        if (at == 0) continue;
        const char before = tag[at - 1];
        const bool okBefore = isWs(before) || (allowNsPrefix && before == ':');
        if (!okBefore) continue;
        size_t p = at + nlen;
        while (p < tag.size() && isWs(tag[p])) p++;
        if (p >= tag.size() || tag[p] != '=') continue;
        p++;
        while (p < tag.size() && isWs(tag[p])) p++;
        if (p >= tag.size() || (tag[p] != '"' && tag[p] != '\'')) continue;
        const char quote = tag[p++];
        const size_t close = tag.find(quote, p);
        if (close != std::string::npos) return tag.substr(p, close - p);
    }
    return {};
}

bool hasImageMagic(const uint8_t* d, size_t n) {
    if (n < 4) return false;
    if (d[0] == 0xFF && d[1] == 0xD8) return true;                             // JPEG
    if (d[0] == 'B' && d[1] == 'M') return true;                               // BMP
    if (d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return true; // PNG
    if (d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return true; // GIF
    return false;
}

class StorageIn : public InFile {
public:
    bool open(const char* path) {
        _f = Storage.open(path, O_RDONLY);
        return (bool)_f;
    }
    ~StorageIn() override {
        if (_f) _f.close();
    }
    bool rewind() override { return _f.seek(0); }
    bool seek(uint64_t pos) override { return _f.seek((uint32_t)pos); }
    int32_t read(void* dst, uint32_t len) override { return _f.read((uint8_t*)dst, len); }
    uint64_t size() const override { return const_cast<FsFile&>(_f).size(); }

private:
    FsFile _f;
};

class StorageOut : public OutFile {
public:
    bool open(const char* path) {
        _f = Storage.open(path, O_RDWR | O_CREAT | O_TRUNC);
        return (bool)_f;
    }
    bool close() {
        if (!_f) return false;
        const bool ok = flush();
        _f.close();
        return ok;
    }
    ~StorageOut() override {
        if (_f) _f.close();
    }
    bool write(const void* data, uint32_t len) override {
        if (!_f || _failed) return false;
        const uint8_t* p = (const uint8_t*)data;
        while (len > 0) {
            const uint32_t k = std::min<uint32_t>(len, sizeof(_buf) - _used);
            memcpy(_buf + _used, p, k);
            _used += k;
            _pos += k;
            p += k;
            len -= k;
            if (_used == sizeof(_buf) && !flush()) return false;
        }
        return true;
    }
    bool seek(uint32_t pos) override {
        if (!flush()) return false;
        if (!_f.seek(pos)) {
            _failed = true;
            return false;
        }
        _pos = pos;
        return true;
    }
    uint32_t tell() override { return _pos; }
    bool failed() const { return _failed; }

private:
    bool flush() {
        if (_used == 0) return true;
        const size_t w = _f.write(_buf, _used);
        _used = 0;
        if (w == 0) {
            _failed = true;
            return false;
        }
        return true;
    }

    FsFile _f;
    uint8_t _buf[512] = {};
    uint32_t _used = 0;
    uint32_t _pos = 0;
    bool _failed = false;
};

struct MobiMeta {
    uint32_t firstImage = 0xFFFFFFFF;
    uint32_t coverOffset = 0xFFFFFFFF;
    uint32_t kf8Boundary = 0xFFFFFFFF;
};

bool parseMobiRecord0Meta(const uint8_t* r, uint32_t n, MobiMeta& meta) {
    if (n < 0x84 || memcmp(r + 16, "MOBI", 4) != 0) return false;
    const uint32_t headerLength = be32(r + 20);
    meta.firstImage = be32(r + 0x6C);
    const uint32_t exthFlags = be32(r + 0x80);
    const uint32_t exth = 16 + headerLength;
    if ((exthFlags & 0x40) && exth + 12 <= n && memcmp(r + exth, "EXTH", 4) == 0) {
        const uint32_t count = be32(r + exth + 8);
        uint32_t p = exth + 12;
        for (uint32_t i = 0; i < count && p + 8 <= n; i++) {
            const uint32_t type = be32(r + p);
            const uint32_t len = be32(r + p + 4);
            if (len < 8 || p + len > n) break;
            if (type == 201 && len - 8 >= 4) meta.coverOffset = be32(r + p + 8);
            if (type == 121 && len - 8 >= 4) meta.kf8Boundary = be32(r + p + 8);
            p += len;
        }
    }
    return true;
}

bool readRecordRange(InFile& in, uint16_t numRecords, uint64_t fileSize, uint32_t recIdx, uint32_t& outOff,
                     uint32_t& outEnd) {
    if (recIdx >= numRecords) return false;
    uint8_t entry[8];
    if (!in.seek(78 + (uint64_t)recIdx * 8) || in.read(entry, 8) != 8) return false;
    outOff = be32(entry);
    if (recIdx + 1 < numRecords) {
        if (in.read(entry, 8) != 8) return false;
        outEnd = be32(entry);
    } else {
        outEnd = (uint32_t)std::min<uint64_t>(fileSize, UINT32_MAX);
    }
    return outOff < outEnd && outEnd <= fileSize;
}

}  // namespace

bool extractFb2Cover(InFile& in, OutFile& out) {
    if (!in.rewind()) return false;

    enum State { TEXT, TAG, B64_DATA };
    State st = TEXT;
    std::string tag;
    std::string coverId;
    bool inCoverpage = false;
    bool b64PadSeen = false;
    uint32_t b64Acc = 0;
    int b64Bits = 0;
    uint8_t dec[512];
    size_t decLen = 0;
    uint64_t totalWritten = 0;
    uint64_t bytesRead = 0;

    auto flushDec = [&]() -> bool {
        if (decLen == 0) return true;
        if (!out.write(dec, (uint32_t)decLen)) return false;
        totalWritten += decLen;
        decLen = 0;
        return true;
    };

    char raw[2048];
    for (;;) {
        Platform::feedWatchdog();
        const int32_t n = in.read(raw, sizeof(raw));
        if (n < 0) return false;
        if (n == 0) break;
        bytesRead += (uint64_t)n;

        int32_t i = 0;
        while (i < n) {
            if (st == TEXT) {
                const char* lt = static_cast<const char*>(memchr(raw + i, '<', (size_t)(n - i)));
                if (!lt) {
                    i = n;
                    break;
                }
                i = (int32_t)(lt - raw) + 1;
                tag.clear();
                st = TAG;
            } else if (st == B64_DATA) {
                const char* lt = static_cast<const char*>(memchr(raw + i, '<', (size_t)(n - i)));
                const int32_t end = lt ? (int32_t)(lt - raw) : n;
                for (int32_t k = i; k < end; k++) {
                    const uint8_t ch = (uint8_t)raw[k];
                    if (ch == '=') {
                        b64PadSeen = true;
                        continue;
                    }
                    if (b64PadSeen) continue;
                    const int8_t v = b64Val(ch);
                    if (v < 0) continue;
                    b64Acc = (b64Acc << 6) | (uint32_t)v;
                    if (++b64Bits == 4) {
                        dec[decLen++] = (uint8_t)(b64Acc >> 16);
                        dec[decLen++] = (uint8_t)(b64Acc >> 8);
                        dec[decLen++] = (uint8_t)b64Acc;
                        b64Acc = 0;
                        b64Bits = 0;
                        if (decLen + 4 > sizeof(dec)) {
                            if (!flushDec()) return false;
                        }
                    }
                }
                if (!lt) {
                    i = n;
                    break;
                }
                if (b64Bits == 2) dec[decLen++] = (uint8_t)(b64Acc >> 4);
                if (b64Bits == 3) {
                    dec[decLen++] = (uint8_t)(b64Acc >> 10);
                    dec[decLen++] = (uint8_t)(b64Acc >> 2);
                }
                if (!flushDec()) return false;
                return totalWritten > 0;
            } else {
                const char c = raw[i++];
                if (c != '>') {
                    if (tag.size() < 1024) tag += c;
                    continue;
                }
                st = TEXT;
                size_t lead = 0;
                while (lead < tag.size() && isWs(tag[lead])) lead++;
                if (lead > 0) tag.erase(0, lead);
                if (tag.empty() || tag[0] == '?' || tag[0] == '!') continue;

                if (coverId.empty()) {
                    if (tagStartsWith(tag, "body")) {
                        // FB2 <description> always precedes <body>; no coverpage found.
                        return false;
                    }
                    if (tagStartsWith(tag, "coverpage")) {
                        if (tag.back() != '/') inCoverpage = true;
                    } else if (tagStartsWith(tag, "/coverpage") || tagStartsWith(tag, "/description")) {
                        inCoverpage = false;
                        if (tagStartsWith(tag, "/description") && coverId.empty()) return false;
                    } else if (inCoverpage && tagStartsWith(tag, "image")) {
                        std::string href = findAttr(tag, "href", true);
                        if (!href.empty() && href[0] == '#') href.erase(0, 1);
                        if (!href.empty()) {
                            coverId = href;
                            inCoverpage = false;
                        }
                    }
                } else {
                    if (tagStartsWith(tag, "binary") && tag.back() != '/') {
                        const std::string id = findAttr(tag, "id", false);
                        if (id == coverId) {
                            st = B64_DATA;
                            b64PadSeen = false;
                            b64Acc = 0;
                            b64Bits = 0;
                            decLen = 0;
                        }
                    }
                }
            }
        }
        if (coverId.empty() && bytesRead > 262144) {
            return false;
        }
    }

    if (st == B64_DATA) {
        if (b64Bits == 2) dec[decLen++] = (uint8_t)(b64Acc >> 4);
        if (b64Bits == 3) {
            dec[decLen++] = (uint8_t)(b64Acc >> 10);
            dec[decLen++] = (uint8_t)(b64Acc >> 2);
        }
        if (!flushDec()) return false;
        return totalWritten > 0;
    }
    return false;
}

bool extractMobiCover(InFile& in, OutFile& out) {
    if (!in.rewind()) return false;
    const uint64_t fileSize = in.size();
    uint8_t pdbHdr[78];
    if (fileSize < 94 || in.read(pdbHdr, 78) != 78) return false;
    const uint16_t numRecords = be16(pdbHdr + 76);
    if (numRecords < 2) return false;

    uint32_t rec0Off = 0, rec0End = 0;
    if (!readRecordRange(in, numRecords, fileSize, 0, rec0Off, rec0End)) return false;
    const uint32_t rec0Len = rec0End - rec0Off;
    if (rec0Len < 0x84 || rec0Len > 131072) return false;

    std::vector<uint8_t> rec0(rec0Len);
    if (!in.seek(rec0Off) || in.read(rec0.data(), rec0Len) != (int32_t)rec0Len) return false;

    MobiMeta meta;
    if (!parseMobiRecord0Meta(rec0.data(), rec0Len, meta)) return false;

    // Combined MOBI+KF8 files sometimes hold EXTH 201 on the KF8 header record.
    if (meta.coverOffset == 0xFFFFFFFF && meta.kf8Boundary != 0xFFFFFFFF && meta.kf8Boundary < numRecords) {
        uint32_t kfOff = 0, kfEnd = 0;
        if (readRecordRange(in, numRecords, fileSize, meta.kf8Boundary, kfOff, kfEnd)) {
            const uint32_t kfLen = kfEnd - kfOff;
            if (kfLen >= 0x84 && kfLen <= 131072) {
                std::vector<uint8_t> kf(kfLen);
                if (in.seek(kfOff) && in.read(kf.data(), kfLen) == (int32_t)kfLen) {
                    MobiMeta kfMeta;
                    if (parseMobiRecord0Meta(kf.data(), kfLen, kfMeta)) {
                        if (kfMeta.coverOffset != 0xFFFFFFFF) meta.coverOffset = kfMeta.coverOffset;
                        if (meta.firstImage == 0xFFFFFFFF) meta.firstImage = kfMeta.firstImage;
                    }
                }
            }
        }
    }

    if (meta.firstImage == 0xFFFFFFFF || meta.coverOffset == 0xFFFFFFFF) return false;
    const uint64_t targetRec = (uint64_t)meta.firstImage + meta.coverOffset;
    if (targetRec >= numRecords) return false;

    uint32_t imgOff = 0, imgEnd = 0;
    if (!readRecordRange(in, numRecords, fileSize, (uint32_t)targetRec, imgOff, imgEnd)) return false;
    const uint32_t imgLen = imgEnd - imgOff;
    if (imgLen < 16 || imgLen > 4 * 1024 * 1024) return false;

    if (!in.seek(imgOff)) return false;
    uint8_t buf[512];
    uint32_t remaining = imgLen;
    bool checkedMagic = false;
    while (remaining > 0) {
        const uint32_t want = std::min<uint32_t>(remaining, sizeof(buf));
        const int32_t got = in.read(buf, want);
        if (got <= 0) return false;
        if (!checkedMagic) {
            if (!hasImageMagic(buf, (size_t)got)) return false;
            checkedMagic = true;
        }
        if (!out.write(buf, (uint32_t)got)) return false;
        remaining -= (uint32_t)got;
    }
    return true;
}

bool extractDirectCover(const String& srcPath, const String& dstPath) {
    const BookFormats::Kind kind = BookFormats::fromPath(srcPath.c_str());
    if (kind != BookFormats::Fb2 && kind != BookFormats::Mobi && kind != BookFormats::Azw3 &&
        kind != BookFormats::Azw) {
        return false;
    }
    StorageIn in;
    if (!in.open(srcPath.c_str())) return false;

    const String tmpPath = dstPath + ".tmp";
    StorageOut out;
    if (!out.open(tmpPath.c_str())) return false;

    bool ok = false;
    if (kind == BookFormats::Fb2) {
        ok = extractFb2Cover(in, out);
    } else {
        ok = extractMobiCover(in, out);
    }
    const bool closed = out.close();
    if (!ok || !closed || out.failed() || out.tell() < 16) {
        Storage.remove(tmpPath.c_str());
        return false;
    }
    if (!Storage.replaceFile(tmpPath.c_str(), dstPath.c_str())) {
        Storage.remove(tmpPath.c_str());
        return false;
    }
    return true;
}

}  // namespace convert
