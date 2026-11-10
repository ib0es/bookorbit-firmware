#include "bookmarks.h"

#include <ArduinoJson.h>
#include <SDCardManager.h>

#include <algorithm>

#include "../hal/storage.h"

BookmarkList Bookmarks;

namespace {
constexpr const char* kDir = "/bookmarks";
}

String BookmarkList::fileFor() const {
    return String(kDir) + "/" + _path.substring(_path.lastIndexOf('/') + 1) + ".json";
}

void BookmarkList::load(const String& bookPath) {
    _items.clear();
    _path = bookPath;
    const String file = fileFor();
    if (!Storage.isAvailable() || !Storage.exists(file.c_str())) return;
    String content = SDCardManager::getInstance().readFile(file.c_str());
    JsonDocument doc;
    if (deserializeJson(doc, content)) {
        Serial.printf("[Bookmarks] %s: unreadable, ignored\n", fileFor().c_str());
        return;
    }
    for (JsonObject o : doc["bookmarks"].as<JsonArray>()) {
        Bookmark b;
        b.spine = o["spine"] | 0;
        b.chars = o["chars"] | 0u;
        b.percent = o["percent"] | 0.0f;
        b.chapter = o["chapter"] | "";
        b.snippet = o["snippet"] | "";
        _items.push_back(b);
    }
    std::sort(_items.begin(), _items.end(), [](const Bookmark& a, const Bookmark& b) {
        return a.spine != b.spine ? a.spine < b.spine : a.chars < b.chars;
    });
}

bool BookmarkList::save() const {
    if (!_path.length()) return false;
    const String file = fileFor();
    if (_items.empty()) {
        if (Storage.exists(file.c_str())) Storage.remove(file.c_str());
        return true;
    }
    Storage.mkdir(kDir);
    JsonDocument doc;
    JsonArray arr = doc["bookmarks"].to<JsonArray>();
    for (const Bookmark& b : _items) {
        JsonObject o = arr.add<JsonObject>();
        o["spine"] = b.spine;
        o["chars"] = b.chars;
        o["percent"] = b.percent;
        o["chapter"] = b.chapter;
        o["snippet"] = b.snippet;
    }
    String out;
    serializeJson(doc, out);
    const String tmp = file + ".tmp";
    FsFile f = Storage.open(tmp.c_str(), O_RDWR | O_CREAT | O_TRUNC);
    if (!f) return false;
    const bool ok = f.write((const uint8_t*)out.c_str(), out.length()) == out.length();
    f.close();
    return ok && Storage.replaceFile(tmp.c_str(), file.c_str());
}

int BookmarkList::findIn(int spine, uint32_t from, uint32_t to) const {
    for (size_t i = 0; i < _items.size(); i++)
        if (_items[i].spine == spine && _items[i].chars >= from && _items[i].chars < to) return (int)i;
    return -1;
}

void BookmarkList::add(const Bookmark& b) {
    auto at = std::lower_bound(_items.begin(), _items.end(), b, [](const Bookmark& x, const Bookmark& y) {
        return x.spine != y.spine ? x.spine < y.spine : x.chars < y.chars;
    });
    _items.insert(at, b);
}

void BookmarkList::remove(int index) {
    if (index >= 0 && index < (int)_items.size()) _items.erase(_items.begin() + index);
}
