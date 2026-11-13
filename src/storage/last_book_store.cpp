#include "last_book_store.h"
#include "json_file.h"
#include "../platform/hal/storage.h"
#include <ArduinoJson.h>

bool LastBookStore::save(const LastBookInfo& info) {
    if (!Storage.isAvailable()) return false;

    Storage.ensureDirectories();

    JsonDocument doc;
    doc["filePath"] = info.filePath;
    doc["title"] = info.title;
    doc["author"] = info.author;
    doc["coverPath"] = info.coverPath;
    doc["progress"] = info.progress;
    doc["page"] = info.page;
    doc["totalPages"] = info.totalPages;
    if (info.fileId > 0) doc["fileId"] = info.fileId;
    if (info.docId.length() > 0) doc["docId"] = info.docId;
    if (info.xpath.length() > 0) doc["xpath"] = info.xpath;
    const bool ok = JsonFile::write("/books/last_book.json", doc);
    if (ok) {
        Serial.printf("[Storage] Last read book saved: %s (page %d)\n", info.title.c_str(), info.page);
    }
    return ok;
}

bool LastBookStore::load(LastBookInfo& outInfo) {
    if (!Storage.isAvailable()) return false;

    JsonDocument doc;
    if (JsonFile::read("/books/last_book.json", doc)) return false;

    if (doc["filePath"].is<const char*>()) outInfo.filePath = doc["filePath"].as<String>();
    if (doc["title"].is<const char*>()) outInfo.title = doc["title"].as<String>();
    if (doc["author"].is<const char*>()) outInfo.author = doc["author"].as<String>();
    if (doc["coverPath"].is<const char*>()) outInfo.coverPath = doc["coverPath"].as<String>();
    if (doc["progress"].is<float>()) outInfo.progress = doc["progress"].as<float>();
    if (doc["page"].is<int>()) outInfo.page = doc["page"].as<int>();
    if (doc["totalPages"].is<int>()) outInfo.totalPages = doc["totalPages"].as<int>();
    if (doc["fileId"].is<int>()) outInfo.fileId = doc["fileId"].as<int>();
    if (doc["docId"].is<const char*>()) outInfo.docId = doc["docId"].as<String>();
    if (doc["xpath"].is<const char*>()) outInfo.xpath = doc["xpath"].as<String>();

    Serial.printf("[Storage] Last read book loaded: %s (page %d/%d)\n", outInfo.title.c_str(), outInfo.page,
                  outInfo.totalPages);
    return (outInfo.filePath.length() > 0);
}
