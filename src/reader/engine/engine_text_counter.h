#pragma once

// Counts a chapter's characters the way FreeInkBook's layout engine does, so an
// EPUB CFI (a DOM path) maps to the engine's position unit: (spine item, character
// offset of the chapter's extracted text), the `charStart` of its pages.
//
// Fed the raw XHTML bytes by EpubCfi::Walker. Mirrors LayoutEngine
// (ChapterLayout.cpp): only <body> text counts, <head>/<style>/<script>/<title>
// are skipped, runs of ASCII whitespace collapse to one space that is dropped at
// a paragraph start and end, every block element (p, div, h1..h6, li, ...)
// starts a new paragraph, <br> is one character, an entity is one character,
// counts are code points. Not mirrored: CSS display:none, and the engine's
// splitting of paragraphs longer than its buffer (one space less per split);
// both are rare and shift a position by a few characters at most.
//
// Pure C++ (no Arduino); host-tested in test/host/test_engine_text_counter.cpp,
// and against the engine itself on real books.

#include <cstdint>
#include <string>

#include "../epub_cfi.h"

class EngineTextCounter : public EpubCfi::TextSink {
public:
    void feed(const char* data, size_t len) override;
    // Characters before the next one the chapter text gets (a pending
    // collapsed space included: it is committed once a character follows).
    uint32_t emitted() const override { return _committed + _parChars + (_pendingSpace ? 1 : 0); }

    // Characters committed so far, without a pending space: what the engine
    // records for an id="" anchor met here.
    uint32_t committed() const { return _committed + _parChars; }

    // Block elements as in ChapterLayout.cpp (isBlockElement).
    static bool isBlock(const std::string& name);

private:
    enum class State { Text, Tag, Entity, Comment, CData };
    void onTag();
    void flushParagraph();
    void textChar(bool whitespace, bool startsCodepoint);

    State _state = State::Text;
    std::string _tag;
    std::string _special;
    bool _inBody = false;
    int _suppress = 0;         // inside head / style / script / title
    uint32_t _committed = 0;   // characters of finished paragraphs
    uint32_t _parChars = 0;    // characters of the current paragraph
    bool _pendingSpace = false;
};
