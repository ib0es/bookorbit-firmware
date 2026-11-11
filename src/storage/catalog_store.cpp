#include "catalog_store.h"
#include "json_file.h"
#include "../core/book_formats.h"
#include "../platform/hal/storage.h"
#include <ArduinoJson.h>

bool CatalogStore::save(const std::vector<BookMetadata>& catalog) {
    if (!Storage.isAvailable()) return false;

    Storage.ensureDirectories();

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (const auto& b : catalog) {
        JsonObject obj = arr.add<JsonObject>();
        obj["id"] = b.id;
        obj["fileId"] = b.fileId;
        obj["title"] = b.title;
        obj["author"] = b.author;
        if (b.series.length() > 0) obj["series"] = b.series;
        if (b.seriesIndex > 0) obj["seriesIdx"] = b.seriesIndex;
        if (b.library.length() > 0) obj["library"] = b.library;
        // Formats: [{"f":fileId,"k":kind,"u":url}], "u" only when not canonical.
        JsonArray fa = obj["formats"].to<JsonArray>();
        for (const auto& f : b.formats) {
            JsonObject fo = fa.add<JsonObject>();
            fo["f"] = f.fileId;
            fo["k"] = f.kind;
            if (f.url.length() > 0) fo["u"] = f.url;
        }
        obj["cover"] = b.coverUrl;
    }
    const bool ok = JsonFile::write("/books/catalog.json", doc);
    if (ok) {
        Serial.printf("[Storage] Catalog metadata saved (%d books) to /books/catalog.json\n", (int)catalog.size());
    }
    return ok;
}

bool CatalogStore::load(std::vector<BookMetadata>& outCatalog) {
    outCatalog.clear();
    if (!Storage.isAvailable()) return false;

    JsonDocument doc;
    if (JsonFile::read("/books/catalog.json", doc)) return false;   // the next sync writes a new one

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        BookMetadata b;
        if (obj["id"].is<const char*>()) b.id = obj["id"].as<String>();
        if (obj["fileId"].is<int>()) b.fileId = obj["fileId"].as<int>();
        if (obj["title"].is<const char*>()) b.title = obj["title"].as<String>();
        if (obj["author"].is<const char*>()) b.author = obj["author"].as<String>();
        if (obj["series"].is<const char*>()) b.series = obj["series"].as<String>();
        if (obj["seriesIdx"].is<int>()) b.seriesIndex = obj["seriesIdx"].as<int>();
        if (obj["library"].is<const char*>()) b.library = obj["library"].as<String>();
        if (obj["formats"].is<JsonArray>()) {
            for (JsonObject fo : obj["formats"].as<JsonArray>()) {
                BookFormatEntry f;
                f.fileId = fo["f"] | 0;
                f.kind = fo["k"] | 0;
                if (fo["u"].is<const char*>()) f.url = fo["u"].as<String>();
                if (f.fileId > 0 || f.url.length() > 0) b.formats.push_back(f);
            }
        } else if (obj["url"].is<const char*>()) {
            // Catalog of older firmware: one address per book (EPUB, or TXT by its name).
            BookFormatEntry f;
            f.fileId = b.fileId;
            f.url = obj["url"].as<String>();
            f.kind = (uint8_t)(f.url.indexOf(".txt") > 0 ? BookFormats::Txt : BookFormats::Epub);
            b.formats.push_back(f);
        }
        if (obj["cover"].is<const char*>()) b.coverUrl = obj["cover"].as<String>();
        // "progress", "page", "xpath", "srvUpdated" of older firmware: server
        // snapshots of books not on the card, no longer kept.
        if (b.title.length() > 0) {
            outCatalog.push_back(b);
        }
    }

    Serial.printf("[Storage] Loaded %d books metadata from /books/catalog.json\n", (int)outCatalog.size());
    return true;
}
