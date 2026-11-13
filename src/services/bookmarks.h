#pragma once

// Bookmarks of a book, kept on the card in /bookmarks/<book file name>.json
// (not in the book's cache directory: that is emptied when the book is
// converted again). A bookmark is a position as the reader keeps it (spine
// item, character in it), which survives any font or layout change, plus what
// the list shows: the percentage, the chapter, the page's first words.
// Synced with the Book Orbit web reader's bookmarks (EPUB books): a bookmark
// also keeps its EPUB CFI and its id on the server; a bookmark that came from
// the server keeps only the CFI until the book is open to place it (spine -1).

#include <Arduino.h>

#include <vector>

struct Bookmark {
    int spine = 0;          // -1: not placed yet (from the server, only `cfi` known)
    uint32_t chars = 0;
    float percent = 0;
    String chapter;
    String snippet;
    String cfi;             // "" until worked out (bookmarks made before syncing)
    int serverId = 0;       // 0: not on the server yet
    bool placed() const { return spine >= 0; }
};

// A bookmark as the server lists it.
struct RemoteBookmark {
    int id = 0;
    String cfi;
    String title;
};

class BookmarkList {
public:
    // The book's bookmarks, sorted by position (none if it has no file).
    void load(const String& bookPath);
    bool save() const;
    const String& bookPath() const { return _path; }

    const std::vector<Bookmark>& items() const { return _items; }
    int count() const { return (int)_items.size(); }
    // Index of the bookmark inside [spine:from, spine:to), -1 if none.
    int findIn(int spine, uint32_t from, uint32_t to) const;
    void add(const Bookmark& b);   // kept sorted
    void remove(int index);        // one on the server is deleted there at the next sync
    void clear() {
        _items.clear();
        _deleted.clear();
        _path = "";
    }

    // Sync. Server ids still to delete there:
    const std::vector<int>& deletedOnDevice() const { return _deleted; }
    void forgetDeleted(int serverId);
    // Takes the server's list: what was deleted there goes here, what is new
    // there comes in (not placed yet), a bookmark not uploaded yet that the
    // server has at the same CFI is linked to it. True if anything changed.
    bool mergeRemote(const std::vector<RemoteBookmark>& remote);
    void setServerId(int index, int serverId) { _items[index].serverId = serverId; }
    // For placing and filling CFIs while the book is open (then sort()).
    std::vector<Bookmark>& mutableItems() { return _items; }
    void sort();

private:
    String fileFor() const;
    String _path;
    std::vector<Bookmark> _items;
    std::vector<int> _deleted;
};

extern BookmarkList Bookmarks;   // of the open book

// Works out, for the open book's list, the places of bookmarks that came from
// the server and the CFIs of ones that have none; saves if anything changed.
// Needs the book open in the reader (reads chapters): not while Wi-Fi is up.
void placeBookmarks(BookmarkList& list);
