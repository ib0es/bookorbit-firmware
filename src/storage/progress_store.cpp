#include "progress_store.h"
#include <ArduinoJson.h>
#include <SDCardManager.h>
#include "../platform/hal/storage.h"
#include "json_file.h"

namespace {

void writePos(JsonObject o, const ProgressSync::Position& p) {
    o["p"] = p.percent;
    if (p.page > 0) o["pg"] = p.page;
    if (!p.xpath.empty()) o["x"] = p.xpath.c_str();
    if (!p.cfi.empty()) o["c"] = p.cfi.c_str();
}

ProgressSync::Position readPos(JsonObjectConst o) {
    ProgressSync::Position p;
    p.percent = o["p"] | 0.0f;
    p.page = o["pg"] | 0;
    p.xpath = o["x"] | "";
    p.cfi = o["c"] | "";
    return p;
}

}  // namespace

bool ProgressStore::load() {
    _records.clear();
    _modified = false;
    _readOnly = false;
    if (!Storage.isAvailable()) return false;

    JsonDocument doc;
    const DeserializationError err = JsonFile::read(kPath, doc);
    if (err == DeserializationError::EmptyInput && !Storage.exists(kPath)) return false;   // none yet
    if (err == DeserializationError::NoMemory) {
        // The file is fine, the heap is not: never write a shorter list over it.
        Serial.println("[Progress] progress.json does not fit in memory now: positions are not saved this session.");
        _readOnly = true;
        return false;
    }
    if (err) {
        Serial.println("[Progress] progress.json is corrupt, starting empty (the old file is kept as .bad).");
        JsonFile::keepUnreadable(kPath);
        return false;
    }
    for (JsonObjectConst o : doc["books"].as<JsonArrayConst>()) {
        BookProgressRecord r;
        r.fileId = o["fileId"] | 0;
        r.bookId = o["bookId"] | 0;
        r.filePath = o["path"] | "";
        r.title = o["title"] | "";
        r.state.local = readPos(o["local"]);
        r.state.localDirty = o["dirty"] | false;
        r.state.localChangedAt = o["changedAt"] | 0u;
        r.state.hasBase = !o["base"].isNull();
        if (r.state.hasBase) r.state.base = readPos(o["base"]);
        r.state.baseServerUpdatedAt = o["baseUpdatedAt"] | 0u;
        _records.push_back(r);
    }
    Serial.printf("[Progress] Loaded %u book records.\n", (unsigned)_records.size());
    return true;
}

bool ProgressStore::save() {
    if (!_modified) return true;
    if (_readOnly || !Storage.isAvailable()) return false;
    Storage.ensureDirectories();

    JsonDocument doc;
    doc["version"] = 1;
    JsonArray arr = doc["books"].to<JsonArray>();
    for (const auto& r : _records) {
        JsonObject o = arr.add<JsonObject>();
        if (r.fileId > 0) o["fileId"] = r.fileId;
        if (r.bookId > 0) o["bookId"] = r.bookId;
        o["path"] = r.filePath;
        o["title"] = r.title;
        writePos(o["local"].to<JsonObject>(), r.state.local);
        if (r.state.localDirty) o["dirty"] = true;
        if (r.state.localChangedAt) o["changedAt"] = r.state.localChangedAt;
        if (r.state.hasBase) writePos(o["base"].to<JsonObject>(), r.state.base);
        if (r.state.baseServerUpdatedAt) o["baseUpdatedAt"] = r.state.baseServerUpdatedAt;
    }
    const bool ok = JsonFile::write(kPath, doc);
    if (ok) _modified = false;
    return ok;
}

BookProgressRecord* ProgressStore::find(int fileId, const String& filePath) {
    if (fileId > 0) {
        for (auto& r : _records) {
            if (r.fileId == fileId) return &r;
        }
    }
    if (filePath.length() > 0) {
        for (auto& r : _records) {
            if (r.filePath.equalsIgnoreCase(filePath)) return &r;
        }
    }
    return nullptr;
}

bool ProgressStore::remove(const String& filePath) {
    if (filePath.length() == 0) return false;
    bool removed = false;
    for (auto it = _records.begin(); it != _records.end();) {
        if (it->filePath.equalsIgnoreCase(filePath)) {
            it = _records.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    if (removed) _modified = true;
    return removed;
}

BookProgressRecord& ProgressStore::getOrCreate(int fileId, int bookId, const String& filePath, const String& title) {
    BookProgressRecord* r = find(fileId, filePath);
    if (!r) {
        _records.push_back(BookProgressRecord());
        r = &_records.back();
        _modified = true;
    }
    if (fileId > 0 && r->fileId != fileId) { r->fileId = fileId; _modified = true; }
    if (bookId > 0 && r->bookId != bookId) { r->bookId = bookId; _modified = true; }
    if (filePath.length() > 0 && r->filePath != filePath) { r->filePath = filePath; _modified = true; }
    if (title.length() > 0 && r->title != title) { r->title = title; _modified = true; }
    return *r;
}
