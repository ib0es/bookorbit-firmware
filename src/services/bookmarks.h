#pragma once

// Bookmarks of a book, kept on the card in /bookmarks/<book file name>.json
// (not in the book's cache directory: that is emptied when the book is
// converted again). A bookmark is a position as the reader keeps it (spine
// item, character in it), which survives any font or layout change, plus what
// the list shows: the percentage, the chapter, the page's first words.
// Device-only: they are not synced.

#include <Arduino.h>

#include <vector>

struct Bookmark {
    int spine = 0;
    uint32_t chars = 0;
    float percent = 0;
    String chapter;
    String snippet;
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
    void remove(int index);
    void clear() { _items.clear(); _path = ""; }

private:
    String fileFor() const;
    String _path;
    std::vector<Bookmark> _items;
};

extern BookmarkList Bookmarks;   // of the open book
