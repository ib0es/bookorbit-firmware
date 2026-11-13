#pragma once

// EPUB CFI (Canonical Fragment Identifier) support, the position format of the
// Book Orbit web reader, e.g. "epubcfi(/6/34!/4/2[id12]/2/2/1:0)":
//   /6/34      -> the 17th <itemref> of the OPF spine (34 / 2),
//   !/4/2/2/2  -> element path inside that XHTML (even = element child #n/2),
//   /1:0       -> odd step = text between element children, ":0" = character offset.
//
// The reader stores converted text, not the DOM, so a CFI is mapped by walking the
// chapter's XHTML: every byte goes both through this walker (which knows the DOM
// path) and through a TextSink (the same HTML stripper that produced the cached
// text, counting its output). The sink's byte count at the CFI point is the offset
// inside the chapter's text, exactly as stored in the cache.
//
// Pure C++ (no Arduino); host-tested in test/host/test_epub_cfi.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace EpubCfi {

struct Location {
    int ordinal = 0;              // 1-based <itemref> number in the spine
    std::vector<int> steps;       // path after '!' (e.g. {4, 2, 2, 2, 1})
    int offset = 0;               // character offset in the final text step (UTF-16 units)
};

// Parses "epubcfi(...)" (assertions [..], ranges "a,b,c" -> start, side bias ;s=..).
bool parse(const std::string& cfi, Location& out);

// "epubcfi(/6/<2*ordinal>!/<steps>:<offset>)".
std::string format(const Location& loc);

// Text produced from the XHTML bytes fed so far.
class TextSink {
public:
    virtual ~TextSink() = default;
    virtual void feed(const char* data, size_t len) = 0;
    virtual uint32_t emitted() const = 0;
};

class Walker {
public:
    // CFI -> text offset: stops at `steps`/`offset`; result in textOffset().
    static Walker toOffset(TextSink& sink, const std::vector<int>& steps, int offset);
    // Text offset -> CFI: stops at the first non-space character emitted at or
    // after `target`; result in location() (ordinal left 0).
    static Walker toLocation(TextSink& sink, uint32_t target);

    void feed(const char* data, size_t len);   // no-op once done()
    void finish();                             // end of document
    bool done() const { return _done; }
    bool reached() const { return _reached; }  // the point was inside the document
    uint32_t textOffset() const { return _result; }
    const Location& location() const { return _loc; }

private:
    enum class Mode { ToOffset, ToLocation };
    enum class State { Text, Entity, Tag, Comment, CData };

    Walker(TextSink& sink, Mode mode) : _sink(sink), _mode(mode) {}

    void onTagEnd();                       // a complete "<...>" was fed
    bool textCharStarts(bool whitespace);  // before feeding a text character; true = stop here
    void stop(bool reached);
    // -1 / 0 / +1: document order of `node` relative to the target path.
    int compareToTarget(const std::vector<int>& node) const;
    std::vector<int> textNodePath() const;

    TextSink& _sink;
    Mode _mode;
    State _state = State::Text;
    bool _done = false, _reached = false;
    uint32_t _result = 0;
    Location _loc;

    std::vector<int> _targetSteps;
    int _targetOffset = 0;
    uint32_t _targetText = 0;

    std::string _tag;                   // current tag text (without '<')
    std::string _special;               // lookahead for comment / CDATA ends
    std::vector<int> _path;             // index steps of open elements below the root
    std::vector<int> _childElems;       // element children seen per open element (root first)
    bool _inRoot = false;
    bool _inHead = false;
    int _textChars = 0;                 // UTF-16 units into the current text chunk
    int _textNodeIndex = -1;            // odd index of the current text chunk
    int _utf8Pending = 0;               // continuation bytes still expected
};

}  // namespace EpubCfi
