#include "bookmarks.h"

#include <ArduinoJson.h>
#include <SDCardManager.h>

#include <algorithm>

#include "../platform/hal/storage.h"
#include "../storage/json_file.h"
#include "../reader/reader.h"

BookmarkList Bookmarks;

namespace {
constexpr const char* kDir = "/bookmarks";
}

String BookmarkList::fileFor() const { return String(kDir) + "/" + BookReader::cacheKeyFor(_path) + ".json"; }

void BookmarkList::load(const String& bookPath) {
    _items.clear();
    _deleted.clear();
    _path = bookPath;
    const String file = fileFor();
    if (!Storage.isAvailable()) return;
    JsonDocument doc;
    const DeserializationError err = JsonFile::read(file.c_str(), doc);
    if (err == DeserializationError::EmptyInput && !Storage.exists(file.c_str())) return;   // no bookmarks yet
    if (err) {
        Serial.printf("[Bookmarks] %s: unreadable, ignored\n", file.c_str());
        if (err != DeserializationError::NoMemory) JsonFile::keepUnreadable(file.c_str());
        return;
    }
    for (JsonObject o : doc["bookmarks"].as<JsonArray>()) {
        Bookmark b;
        b.spine = o["spine"] | 0;
        b.chars = o["chars"] | 0u;
        b.percent = o["percent"] | 0.0f;
        b.chapter = o["chapter"] | "";
        b.snippet = o["snippet"] | "";
        b.cfi = o["cfi"] | "";
        b.serverId = o["sid"] | 0;
        _items.push_back(b);
    }
    for (int id : doc["deleted"].as<JsonArray>())
        if (id > 0) _deleted.push_back(id);
    sort();
}

void BookmarkList::sort() {
    std::stable_sort(_items.begin(), _items.end(), [](const Bookmark& a, const Bookmark& b) {
        return a.spine != b.spine ? a.spine < b.spine : a.chars < b.chars;
    });
}

bool BookmarkList::save() const {
    if (!_path.length()) return false;
    const String file = fileFor();
    if (_items.empty() && _deleted.empty()) {
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
        if (b.cfi.length()) o["cfi"] = b.cfi;
        if (b.serverId > 0) o["sid"] = b.serverId;
    }
    if (!_deleted.empty()) {
        JsonArray del = doc["deleted"].to<JsonArray>();
        for (int id : _deleted) del.add(id);
    }
    return JsonFile::write(file.c_str(), doc);
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
    if (index < 0 || index >= (int)_items.size()) return;
    if (_items[index].serverId > 0) _deleted.push_back(_items[index].serverId);
    _items.erase(_items.begin() + index);
}

void BookmarkList::forgetDeleted(int serverId) {
    _deleted.erase(std::remove(_deleted.begin(), _deleted.end(), serverId), _deleted.end());
}

bool BookmarkList::mergeRemote(const std::vector<RemoteBookmark>& remote) {
    bool changed = false;
    auto onServer = [&](int id) {
        for (const auto& r : remote)
            if (r.id == id) return true;
        return false;
    };
    // Deleted on the server (or on another device).
    for (size_t i = 0; i < _items.size();) {
        if (_items[i].serverId > 0 && !onServer(_items[i].serverId)) {
            Serial.printf("[Bookmarks] %d deleted on the server\n", _items[i].serverId);
            _items.erase(_items.begin() + i);
            changed = true;
        } else {
            i++;
        }
    }
    for (const auto& r : remote) {
        if (std::find(_deleted.begin(), _deleted.end(), r.id) != _deleted.end()) continue;   // deleted here
        bool known = false;
        for (auto& b : _items) {
            if (b.serverId == r.id) {
                known = true;
                break;
            }
        }
        if (known) continue;
        for (auto& b : _items) {   // made here, the server has one there already
            if (b.serverId == 0 && b.cfi.length() && b.cfi == r.cfi) {
                b.serverId = r.id;
                known = changed = true;
                break;
            }
        }
        if (known) continue;
        Bookmark b;
        b.spine = -1;
        b.cfi = r.cfi;
        b.chapter = r.title;
        b.serverId = r.id;
        _items.push_back(b);
        changed = true;
        Serial.printf("[Bookmarks] %d from the server: %s\n", r.id, r.cfi.c_str());
    }
    if (changed) sort();
    return changed;
}

void placeBookmarks(BookmarkList& list) {
    if (!Reader.isOpened() || list.bookPath().length() == 0) return;
    bool changed = false;
    for (Bookmark& b : list.mutableItems()) {
        if (!b.placed()) {
            int spine = 0;
            uint32_t chars = 0;
            if (!Reader.placeOfCfi(b.cfi, spine, chars)) continue;   // stays listed; tried again
            b.spine = spine;
            b.chars = chars;
            b.percent = Reader.percentAt(spine, chars);
            changed = true;
            Serial.printf("[Bookmarks] %s -> spine %d, char %u\n", b.cfi.c_str(), spine, (unsigned)chars);
        } else if (b.cfi.length() == 0) {
            b.cfi = Reader.cfiAt(b.spine, b.chars);
            changed = b.cfi.length() > 0 || changed;
        }
        if (b.placed() && b.snippet.length() == 0) {   // the server keeps no first words
            b.snippet = Reader.textAt(b.spine, b.chars);
            changed = b.snippet.length() > 0 || changed;
        }
    }
    if (!changed) return;
    list.sort();
    list.save();
}
