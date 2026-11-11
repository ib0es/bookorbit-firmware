#include "json_file.h"

#include <algorithm>

#include "../platform/hal/storage.h"
#include "json_writer.h"

namespace {

// ArduinoJson reader over an open card file, in 512-byte reads.
struct CardFileReader {
    FsFile& f;
    uint8_t buf[512];
    size_t n = 0, at = 0;
    explicit CardFileReader(FsFile& file) : f(file) {}
    bool fill() {
        if (at < n) return true;
        const auto r = f.read(buf, sizeof(buf));
        n = r > 0 ? (size_t)r : 0;
        at = 0;
        return n > 0;
    }
    int read() { return fill() ? buf[at++] : -1; }
    size_t readBytes(char* dst, size_t len) {
        size_t got = 0;
        while (got < len && fill()) {
            size_t k = std::min(len - got, n - at);
            memcpy(dst + got, buf + at, k);
            at += k;
            got += k;
        }
        return got;
    }
};

DeserializationError parseFile(const char* path, JsonDocument& doc) {
    doc.clear();
    FsFile f = Storage.open(path, O_RDONLY);
    if (!f) return DeserializationError::EmptyInput;
    CardFileReader reader(f);
    const DeserializationError err = deserializeJson(doc, reader);
    f.close();
    return err;
}

int64_t sizeOf(const char* path) {
    FsFile f = Storage.open(path, O_RDONLY);
    if (!f) return -1;
    const int64_t n = (int64_t)f.size();
    f.close();
    return n;
}

}  // namespace

namespace JsonFile {

DeserializationError read(const char* path, JsonDocument& doc) {
    doc.clear();
    if (!Storage.isAvailable()) return DeserializationError::EmptyInput;
    if (!Storage.exists(path)) {
        // Cut between the remove and the rename of a save: the new copy is whole.
        const String tmp = String(path) + ".tmp";
        if (!Storage.exists(tmp.c_str()) || parseFile(tmp.c_str(), doc)) {
            doc.clear();
            return DeserializationError::EmptyInput;
        }
        Serial.printf("[Storage] %s was missing: its complete .tmp taken\n", path);
        Storage.rename(tmp.c_str(), path);
        return DeserializationError::Ok;
    }
    const DeserializationError err = parseFile(path, doc);
    if (err) Serial.printf("[Storage] %s: %s\n", path, err.c_str());
    return err;
}

bool write(const char* path, const JsonDocument& doc, bool pretty) {
    if (!Storage.isAvailable()) return false;
    if (doc.overflowed()) {
        Serial.printf("[Storage] Not enough memory to build %s: the file on the card is kept.\n", path);
        return false;
    }
    const size_t expected = pretty ? measureJsonPretty(doc) : measureJson(doc);
    const String tmp = String(path) + ".tmp";
    FsFile f = Storage.open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC);
    if (!f) {
        Serial.printf("[Storage] Cannot write %s\n", tmp.c_str());
        return false;
    }
    bool ok;
    size_t written;
    {
        JsonFileWriter<FsFile> w(f);
        written = pretty ? serializeJsonPretty(doc, w) : serializeJson(doc, w);
        ok = w.flush() && w.accepted() == expected;
    }
    f.flush();
    f.close();
    // What reached the card (a close that could not write the last block shows here).
    const int64_t onCard = sizeOf(tmp.c_str());
    ok = ok && written == expected && onCard == (int64_t)expected;
    if (!ok) {
        Serial.printf("[Storage] Short write of %s (%u of %u bytes, %d on the card): the file on the card is kept.\n",
                      path, (unsigned)written, (unsigned)expected, (int)onCard);
        Storage.remove(tmp.c_str());
        return false;
    }
    if (!Storage.replaceFile(tmp.c_str(), path)) {
        Serial.printf("[Storage] Cannot move %s into place\n", tmp.c_str());
        Storage.remove(tmp.c_str());
        return false;
    }
    return true;
}

void keepUnreadable(const char* path) {
    if (!Storage.exists(path)) return;
    const String bad = String(path) + ".bad";
    if (Storage.exists(bad.c_str())) Storage.remove(bad.c_str());
    if (Storage.rename(path, bad.c_str())) Serial.printf("[Storage] Unreadable %s kept as %s\n", path, bad.c_str());
}

}  // namespace JsonFile
