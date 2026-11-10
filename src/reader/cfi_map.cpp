#include "cfi_map.h"

#include <cstdlib>
#include <cstring>

namespace {

// Splits a line into numbers ("4/2/8" is one token: its own vector).
bool readInt(const char*& p, const char* end, int& v) {
    while (p < end && *p == ' ') p++;
    if (p >= end || *p < '0' || *p > '9') return false;
    v = 0;
    while (p < end && *p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return true;
}

bool readPath(const char*& p, const char* end, std::vector<int>& out) {
    out.clear();
    int v = 0;
    if (!readInt(p, end, v)) return false;
    out.push_back(v);
    while (p < end && *p == '/') {
        p++;
        if (!readInt(p, end, v)) return false;
        out.push_back(v);
    }
    return true;
}

// Position of an element step / text slot at one level, comparable: element i
// (step 2i+2) comes after slot 2i+1.
bool before(const std::vector<int>& a, const std::vector<int>& start) {
    // Whether path `a` lies before the point `start` (a path whose last step
    // is the slot right before the first element of the file).
    for (size_t j = 0; j < start.size(); j++) {
        if (j >= a.size()) return true;   // an ancestor of the start: before it
        if (a[j] != start[j]) return a[j] < start[j];
    }
    return false;
}

}  // namespace

bool CfiMap::parse(const char* text, size_t len) {
    clear();
    const char* p = text;
    const char* end = text + len;
    bool header = false;
    while (p < end) {
        const char* eol = static_cast<const char*>(memchr(p, '\n', end - p));
        if (!eol) eol = end;
        if (!header) {
            if (eol - p < 8 || strncmp(p, "fb2cfi 1", 8) != 0) return false;
            header = true;
        } else if (*p == 'F') {
            const char* q = p + 1;
            int file = 0, section = 0, v = 0;
            if (!readInt(q, eol, file) || !readInt(q, eol, section) || file < 1) {
                clear();
                return false;
            }
            File f;
            f.section = section;
            while (readInt(q, eol, v)) f.off.push_back(v);
            if (_files.size() < (size_t)file) _files.resize(file);
            _files[file - 1] = f;
        } else if (*p == 'L') {
            const char* q = p + 1;
            Long l;
            int v = 0;
            if (!readInt(q, eol, l.file) || !readPath(q, eol, l.path)) {
                clear();
                return false;
            }
            while (readInt(q, eol, v)) {
                int b = 0;
                if (!readInt(q, eol, b)) break;
                l.m.push_back(v);
                l.b.push_back(b);
            }
            if (!l.m.empty()) _long.push_back(l);
        }
        p = eol + 1;
    }
    for (const File& f : _files) {
        if (f.section < 0) {   // a file the map does not name
            clear();
            return false;
        }
    }
    return header && !_files.empty();
}

bool CfiMap::toPublic(EpubCfi::Location& loc) const {
    const int file = loc.ordinal;
    if (file < 1 || file > (int)_files.size()) return false;
    std::vector<int> s = loc.steps;
    if (s.size() < 2 || s[0] != 4 || s[1] != 2) {
        // Not inside the root element (the <body> itself): its start.
        s = {4, 2};
        loc.offset = 0;
    }
    // A paragraph in pieces: piece i's content belongs to foliate's one p.
    for (const Long& l : _long) {
        if (l.file != file || s.size() < l.path.size()) continue;
        if (!std::equal(l.path.begin(), l.path.end(), s.begin())) continue;
        std::vector<int> out(l.path.begin(), l.path.end());
        if (s.size() > l.path.size() && s[l.path.size()] % 2 == 0) {
            const int piece = s[l.path.size()] / 2 - 1;
            if (piece >= 0 && piece < (int)l.m.size()) {
                if (s.size() > l.path.size() + 1) {
                    int r = s[l.path.size() + 1];
                    const bool firstSlot = r == 1;
                    r += 2 * l.m[piece];
                    out.push_back(r);
                    for (size_t j = l.path.size() + 2; j < s.size(); j++) out.push_back(s[j]);
                    if (firstSlot && s.size() == l.path.size() + 2) loc.offset += l.b[piece];
                } else {   // the piece itself: where it starts
                    out.push_back(2 * l.m[piece] + 1);
                    loc.offset = l.b[piece];
                }
            } else {
                loc.offset = 0;
            }
        } else {
            loc.offset = 0;   // the div, or the space between pieces: the paragraph
        }
        s = out;
        break;
    }
    // The file inside its section: shift along the chain of first elements.
    const File& f = _files[file - 1];
    for (size_t j = 0; j < f.off.size() && 2 + j < s.size(); j++) {
        const int ours = s[2 + j];
        s[2 + j] = ours + 2 * f.off[j];
        if (ours != 2) break;   // past the first element: deeper levels are whole
    }
    loc.steps = s;
    loc.ordinal = f.section + 1;
    return true;
}

bool CfiMap::toLocal(EpubCfi::Location& loc) const {
    const int section = loc.ordinal - 1;
    std::vector<int> s = loc.steps;
    if (s.size() < 2 || s[0] != 4 || s[1] != 2) {
        s = {4, 2};
        loc.offset = 0;
    }
    // The last file of the section that starts at or before the place.
    int file = 0;
    for (size_t i = 0; i < _files.size(); i++) {
        const File& f = _files[i];
        if (f.section != section) continue;
        if (!f.off.empty()) {
            std::vector<int> start = {4, 2};
            for (size_t j = 0; j < f.off.size(); j++)
                start.push_back(j + 1 < f.off.size() ? 2 * f.off[j] + 2 : 2 * f.off[j] + 1);
            if (before(s, start)) continue;
        }
        file = (int)i + 1;
    }
    if (file == 0) return false;
    const File& f = _files[file - 1];
    for (size_t j = 0; j < f.off.size() && 2 + j < s.size(); j++) {
        const int theirs = s[2 + j] - 2 * f.off[j];
        s[2 + j] = theirs;
        if (theirs != 2) break;
    }
    for (const Long& l : _long) {
        if (l.file != file || s.size() < l.path.size()) continue;
        if (!std::equal(l.path.begin(), l.path.end(), s.begin())) continue;
        std::vector<int> out(l.path.begin(), l.path.end());
        if (s.size() == l.path.size()) {
            out.push_back(2);   // the paragraph: its first piece
            loc.offset = 0;
        } else {
            const int r = s[l.path.size()];
            const bool terminal = s.size() == l.path.size() + 1;
            // The piece that holds it: the last starting at or before it.
            int piece = 0;
            for (int i = 0; i < (int)l.m.size(); i++) {
                const int slot = 2 * l.m[i] + 1;
                if (slot < r || (slot == r && (!terminal || l.b[i] <= loc.offset))) piece = i;
            }
            const int slot = 2 * l.m[piece] + 1;
            out.push_back(2 * (piece + 1));
            out.push_back(r - 2 * l.m[piece]);
            for (size_t j = l.path.size() + 1; j < s.size(); j++) out.push_back(s[j]);
            if (r == slot && terminal) loc.offset -= l.b[piece];
        }
        s = out;
        break;
    }
    loc.steps = s;
    loc.ordinal = file;
    return true;
}
