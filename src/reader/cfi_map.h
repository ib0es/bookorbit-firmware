#pragma once
// CFIs of a book converted here (FB2, AZW3), in the web reader's terms.
//
// The Book Orbit web reader opens an FB2 with foliate-js: each element of the
// first <body> is a section of its own, each later <body> another. The
// converter (fb2_to_epub) lays every section out with the same elements and
// text, but cuts a long one into several files of the EPUB, and a long
// paragraph into several <p> (in one <div>, in place of foliate's one <p>).
// It writes where each piece stands in foliate's layout:
//
//   fb2cfi 1
//   F <file> <section> [<o1> ... <on>]
//       file (1-based, = its spine item) is foliate's section `section`
//       (0-based). Without offsets it starts the section. With them it starts
//       inside: below the root, the file's first element at depth j is
//       foliate's element o_j among its siblings (the sections open when the
//       file was cut, then the next child of the innermost one).
//   L <file> <steps> <m0> <b0> [<m1> <b1> ...]
//       a paragraph at element path <steps> of the file ("4/2/8/6": from
//       <body>) cut into pieces: piece i starts after m_i of foliate's
//       paragraph's child elements, b_i UTF-16 units into the text there.
//
// "cfimap 2" (AZW3/KF8: each foliate section is a file of the book) is the
// same with the offsets counted from <body>'s children: a section's top level
// is <body>'s, not one root element's.
//
// Pure (no Arduino): host-tested in test/host/test_cfi_map.cpp.
#include <cstddef>
#include <string>
#include <vector>

#include "epub_cfi.h"

class CfiMap {
public:
    bool parse(const char* text, size_t len);   // false: not a map (then empty())
    bool empty() const { return _files.empty(); }
    void clear() {
        _files.clear();
        _long.clear();
        _byDepth.clear();
    }

    // In place: a location in the converted EPUB -> the web reader's, and back.
    // False when the location is outside what the map knows (left unchanged).
    bool toPublic(EpubCfi::Location& loc) const;
    bool toLocal(EpubCfi::Location& loc) const;

private:
    struct File {
        int section = -1;
        std::vector<int> off;
    };
    struct Long {
        int file = 0;
        std::vector<int> path;                 // element steps from <body>, e.g. {4, 2, 8, 6}
        std::vector<int> m, b;                 // per piece
    };
    std::vector<File> _files;                  // [file - 1]; offsets from <body>'s children on
    bool _belowRoot = false;
    std::vector<Long> _long;
    std::vector<size_t> _byDepth;              // _long by path length
};
