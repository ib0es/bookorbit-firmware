// Host unit tests for fast cover extraction (FB2 / MOBI / AZW3).
// Run: test/host/run.sh
#include "convert/cover_extract.h"
#include "platform/hal/storage.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

void simPumpEvents() {}

namespace Platform {
void feedWatchdog() {}
}

namespace {

class MemIn : public convert::InFile {
public:
    explicit MemIn(std::vector<uint8_t> data) : _data(std::move(data)) {}
    bool rewind() override { _pos = 0; return true; }
    bool seek(uint64_t pos) override {
        if (pos > _data.size()) return false;
        _pos = (size_t)pos;
        return true;
    }
    int32_t read(void* dst, uint32_t len) override {
        if (_pos >= _data.size()) return 0;
        const size_t avail = _data.size() - _pos;
        const size_t take = avail < len ? avail : len;
        memcpy(dst, _data.data() + _pos, take);
        _pos += take;
        return (int32_t)take;
    }
    uint64_t size() const override { return _data.size(); }

private:
    std::vector<uint8_t> _data;
    size_t _pos = 0;
};

class FileIn : public convert::InFile {
public:
    bool open(const fs::path& p) {
        _f = fopen(p.c_str(), "rb");
        if (!_f) return false;
        fseek(_f, 0, SEEK_END);
        _sz = (uint64_t)ftell(_f);
        fseek(_f, 0, SEEK_SET);
        return true;
    }
    ~FileIn() override { if (_f) fclose(_f); }
    bool rewind() override { return fseek(_f, 0, SEEK_SET) == 0; }
    bool seek(uint64_t pos) override { return fseek(_f, (long)pos, SEEK_SET) == 0; }
    int32_t read(void* dst, uint32_t len) override {
        return (int32_t)fread(dst, 1, len, _f);
    }
    uint64_t size() const override { return _sz; }

private:
    FILE* _f = nullptr;
    uint64_t _sz = 0;
};

class MemOut : public convert::OutFile {
public:
    bool write(const void* data, uint32_t len) override {
        const uint8_t* p = (const uint8_t*)data;
        if (_pos == _data.size()) {
            _data.insert(_data.end(), p, p + len);
        } else {
            if (_pos + len > _data.size()) _data.resize(_pos + len);
            memcpy(_data.data() + _pos, p, len);
        }
        _pos += len;
        return true;
    }
    bool seek(uint32_t pos) override {
        if (pos > _data.size()) return false;
        _pos = pos;
        return true;
    }
    uint32_t tell() override { return _pos; }
    const std::vector<uint8_t>& bytes() const { return _data; }

private:
    std::vector<uint8_t> _data;
    uint32_t _pos = 0;
};

void putBe16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = (uint8_t)(x >> 8);
    v[off + 1] = (uint8_t)x;
}

void putBe32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    v[off] = (uint8_t)(x >> 24);
    v[off + 1] = (uint8_t)(x >> 16);
    v[off + 2] = (uint8_t)(x >> 8);
    v[off + 3] = (uint8_t)x;
}

std::vector<uint8_t> makeSyntheticMobi(const std::vector<uint8_t>& coverImg, bool withCover) {
    // Record 0: 16B PalmDOC + 0xE4 MOBI header + EXTH (1 record: 201 coverOffset=1)
    // Record 1: dummy text
    // Record 2: dummy image 0
    // Record 3: actual cover image (firstImage=2, coverOffset=1 -> rec 3)
    const uint16_t nRec = 4;
    std::vector<uint8_t> rec0(16 + 0xE4 + (withCover ? 24 : 0), 0);
    memcpy(rec0.data() + 16, "MOBI", 4);
    putBe32(rec0, 20, 0xE4);        // headerLength
    putBe32(rec0, 0x6C, 2);         // firstImage = rec 2
    if (withCover) {
        putBe32(rec0, 0x80, 0x40);  // exthFlags bit 6
        const size_t exth = 16 + 0xE4;
        memcpy(rec0.data() + exth, "EXTH", 4);
        putBe32(rec0, exth + 4, 24);
        putBe32(rec0, exth + 8, 1);     // 1 record
        putBe32(rec0, exth + 12, 201);  // coverOffset
        putBe32(rec0, exth + 16, 12);   // len = 12
        putBe32(rec0, exth + 20, 1);    // offset from firstImage
    }
    std::vector<uint8_t> rec1(16, 'A');
    std::vector<uint8_t> rec2(16, 'B');

    const uint32_t headerSize = 78 + nRec * 8;
    const uint32_t off0 = headerSize;
    const uint32_t off1 = off0 + (uint32_t)rec0.size();
    const uint32_t off2 = off1 + (uint32_t)rec1.size();
    const uint32_t off3 = off2 + (uint32_t)rec2.size();

    std::vector<uint8_t> pdb(headerSize, 0);
    putBe16(pdb, 76, nRec);
    putBe32(pdb, 78 + 0 * 8, off0);
    putBe32(pdb, 78 + 1 * 8, off1);
    putBe32(pdb, 78 + 2 * 8, off2);
    putBe32(pdb, 78 + 3 * 8, off3);

    pdb.insert(pdb.end(), rec0.begin(), rec0.end());
    pdb.insert(pdb.end(), rec1.begin(), rec1.end());
    pdb.insert(pdb.end(), rec2.begin(), rec2.end());
    pdb.insert(pdb.end(), coverImg.begin(), coverImg.end());
    return pdb;
}

}  // namespace

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

int main(int argc, char** argv) {
    // 1. Synthetic FB2 with l:href and whitespace-padded base64
    {
        // "\xFF\xD8\xFF\xE0" "JFIF" base64 = "/9j/4EpGSUY="
        const std::string fb2 =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<FictionBook xmlns:l=\"http://www.w3.org/1999/xlink\">"
            "<description><title-info>"
            "<coverpage><image l:href=\"#cov.jpg\"/></coverpage>"
            "<book-title>Test</book-title>"
            "</title-info></description>"
            "<body><section><p>Hello</p></section></body>"
            "<binary id=\"other.jpg\" content-type=\"image/jpeg\">AAAA</binary>"
            "<binary content-type=\"image/jpeg\" id=\"cov.jpg\">\n"
            "  /9j/ 4EpG\r\n  SUY=\n"
            "</binary>"
            "</FictionBook>";
        MemIn in(std::vector<uint8_t>(fb2.begin(), fb2.end()));
        MemOut out;
        CHECK(convert::extractFb2Cover(in, out));
        const std::vector<uint8_t> expected = {0xFF, 0xD8, 0xFF, 0xE0, 'J', 'F', 'I', 'F'};
        CHECK(out.bytes() == expected);
    }

    // 2. FB2 without coverpage returns false cleanly
    {
        const std::string fb2 =
            "<FictionBook><description><title-info><book-title>NoCover</book-title>"
            "</title-info></description><body><section><p>Text</p></section></body>"
            "<binary id=\"x.jpg\" content-type=\"image/jpeg\">/9j/4AAQ</binary></FictionBook>";
        MemIn in(std::vector<uint8_t>(fb2.begin(), fb2.end()));
        MemOut out;
        CHECK(!convert::extractFb2Cover(in, out));
        CHECK(out.bytes().empty());
    }

    // 3. Synthetic MOBI with EXTH 201 cover offset
    {
        std::vector<uint8_t> fakeJpeg(320, 0xAA);
        fakeJpeg[0] = 0xFF;
        fakeJpeg[1] = 0xD8;
        fakeJpeg[2] = 0xFF;
        fakeJpeg[3] = 0xE0;
        const auto pdb = makeSyntheticMobi(fakeJpeg, true);
        MemIn in(pdb);
        MemOut out;
        CHECK(convert::extractMobiCover(in, out));
        CHECK(out.bytes() == fakeJpeg);

        const auto noCov = makeSyntheticMobi(fakeJpeg, false);
        MemIn in2(noCov);
        MemOut out2;
        CHECK(!convert::extractMobiCover(in2, out2));
    }

    // 4. Storage::extractDirectCover atomic output & failure cleanup
    {
        fs::path tmp = fs::temp_directory_path() / ("bookorbit_cov_test_" + std::to_string(getpid()));
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        SDCardManager::getInstance().setRoot(tmp.string());
        Storage.ensureDirectories();

        // Build >300 byte JPEG payload in FB2
        std::vector<uint8_t> jpg(360, 0x11);
        jpg[0] = 0xFF; jpg[1] = 0xD8; jpg[2] = 0xFF; jpg[3] = 0xE0;
        static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string b64;
        for (size_t i = 0; i < jpg.size(); i += 3) {
            uint32_t v = ((uint32_t)jpg[i] << 16) | ((uint32_t)jpg[i + 1] << 8) | jpg[i + 2];
            b64 += kB64[(v >> 18) & 63];
            b64 += kB64[(v >> 12) & 63];
            b64 += kB64[(v >> 6) & 63];
            b64 += kB64[v & 63];
        }
        const std::string fb2 = "<FictionBook><description><title-info><coverpage><image href=\"#c\"/></coverpage>"
                                "</title-info></description><body/><binary id=\"c\">" + b64 + "</binary></FictionBook>";
        {
            std::ofstream f(tmp / "offline" / "sample.fb2", std::ios::binary);
            f.write(fb2.data(), (std::streamsize)fb2.size());
        }
        CHECK(convert::extractDirectCover("/offline/sample.fb2", "/covers/offline#sample.jpg"));
        CHECK(fs::exists(tmp / "covers" / "offline#sample.jpg"));
        CHECK(!fs::exists(tmp / "covers" / "offline#sample.jpg.tmp"));
        CHECK(fs::file_size(tmp / "covers" / "offline#sample.jpg") == 360);

        fs::remove_all(tmp);
    }

    // 5. Real test books (if passed argv[1])
    if (argc > 1) {
        const fs::path booksDir = argv[1];
        const char* fb2Name = "Alice's Adventures in Wonderland - Lewis Carroll - FB2.fb2";
        if (fs::exists(booksDir / fb2Name)) {
            FileIn in;
            CHECK(in.open(booksDir / fb2Name));
            MemOut out;
            CHECK(convert::extractFb2Cover(in, out));
            CHECK(out.bytes().size() > 1000);
            CHECK(out.bytes()[0] == 0xFF && out.bytes()[1] == 0xD8);
        }
        const char* mobiName = "Alice's Adventures in Wonderland - Lewis Carroll - MOBI.mobi";
        if (fs::exists(booksDir / mobiName)) {
            FileIn in;
            CHECK(in.open(booksDir / mobiName));
            MemOut out;
            CHECK(convert::extractMobiCover(in, out));
            CHECK(out.bytes().size() > 1000);
            CHECK(out.bytes()[0] == 0xFF && out.bytes()[1] == 0xD8);
        }
        const char* azw3Name = "Alice's Adventures in Wonderland - Lewis Carroll - AZW3.azw3";
        if (fs::exists(booksDir / azw3Name)) {
            FileIn in;
            CHECK(in.open(booksDir / azw3Name));
            MemOut out;
            CHECK(convert::extractMobiCover(in, out));
            CHECK(out.bytes().size() > 1000);
            CHECK(out.bytes()[0] == 0xFF && out.bytes()[1] == 0xD8);
        }
    }

    printf("OK: %d/%d checks passed\n", g_checks - g_failed, g_checks);
    return g_failed ? 1 : 0;
}
