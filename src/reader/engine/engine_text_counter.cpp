#include "engine_text_counter.h"

#include <cctype>
#include <cstring>

namespace {

bool isSuppressed(const std::string& n) { return n == "head" || n == "style" || n == "script" || n == "title"; }

// "p class=..." -> "p", "/div" -> "div", "svg:image/" -> "image" (local name).
std::string localName(const std::string& tag, bool& closing, bool& selfClosing) {
    size_t i = 0;
    closing = !tag.empty() && tag[0] == '/';
    if (closing) i = 1;
    size_t end = i;
    while (end < tag.size() && !isspace((unsigned char)tag[end]) && tag[end] != '/' && tag[end] != '>') end++;
    std::string name = tag.substr(i, end - i);
    const size_t colon = name.rfind(':');
    if (colon != std::string::npos) name.erase(0, colon + 1);
    for (char& c : name) c = (char)tolower((unsigned char)c);
    selfClosing = tag.size() >= 2 && tag[tag.size() - 2] == '/';
    return name;
}

}  // namespace

bool EngineTextCounter::isBlock(const std::string& n) {
    static const char* const kBlocks[] = {"p",       "h1",      "h2",     "h3",    "h4",         "h5", "h6", "blockquote",
                                          "li",      "div",     "section", "article", "figure", "aside", "figcaption",
                                          "ul",      "ol",      "table",  "tr",    "td",         "th", "dt", "dd"};
    for (const char* b : kBlocks) {
        if (n == b) return true;
    }
    return false;
}

void EngineTextCounter::flushParagraph() {
    _committed += _parChars;
    _parChars = 0;
    _pendingSpace = false;
}

void EngineTextCounter::textChar(bool whitespace, bool startsCodepoint) {
    if (!_inBody || _suppress > 0) return;
    if (whitespace) {
        _pendingSpace = _parChars > 0;   // collapse runs; drop leading whitespace
        return;
    }
    if (!startsCodepoint) return;        // UTF-8 continuation byte
    if (_pendingSpace) {
        _parChars++;
        _pendingSpace = false;
    }
    _parChars++;
}

void EngineTextCounter::onTag() {
    if (_tag.empty() || _tag[0] == '?' || _tag[0] == '!') return;
    bool closing = false, selfClosing = false;
    std::string name = localName(_tag, closing, selfClosing);
    if (_inBody && name == "title") name = "h2";   // a heading in <title> (as the engine reads it)
    if (isSuppressed(name)) {
        if (closing) { if (_suppress > 0) _suppress--; }
        else if (!selfClosing) _suppress++;
        return;
    }
    if (name == "body") {
        if (closing) flushParagraph();
        _inBody = !closing;
        return;
    }
    if (!_inBody || _suppress > 0) return;
    if (isBlock(name) || name == "hr" || (!closing && (name == "img" || name == "image"))) {
        flushParagraph();
    } else if (!closing && name == "br") {
        _parChars++;   // a hard line break is one character ('\n')
    }
}

void EngineTextCounter::feed(const char* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        const char c = data[i];
        switch (_state) {
            case State::Text:
                if (c == '<') {
                    _state = State::Tag;
                    _tag.clear();
                } else if (c == '&') {
                    _state = State::Entity;
                } else {
                    const unsigned char u = (unsigned char)c;
                    textChar(c == ' ' || c == '\t' || c == '\n' || c == '\r', (u & 0xC0) != 0x80);
                }
                break;
            case State::Entity:
                if (c == ';') {
                    textChar(false, true);   // one character, never ASCII whitespace in books
                    _state = State::Text;
                } else if (c == '<') {
                    _state = State::Tag;
                    _tag.clear();
                } else if (c == ' ' || c == '\n' || c == '\t' || c == '\r') {
                    textChar(false, true);   // a bare '&' (repaired by the engine as text)
                    textChar(true, true);
                    _state = State::Text;
                }
                break;
            case State::Tag: {
                _tag += c;
                if (_tag == "!--") { _state = State::Comment; _special.clear(); break; }
                if (_tag == "![CDATA[") { _state = State::CData; _special.clear(); break; }
                if (c == '>') {
                    char quote = 0;
                    for (char t : _tag) {
                        if (quote) { if (t == quote) quote = 0; }
                        else if (t == '"' || t == '\'') quote = t;
                    }
                    if (!quote) {
                        onTag();
                        _state = State::Text;
                    }
                }
                break;
            }
            case State::Comment:
            case State::CData:
                _special += c;
                if (_special.size() > 3) _special.erase(0, _special.size() - 3);
                if ((_state == State::Comment && _special == "-->") || (_state == State::CData && _special == "]]>"))
                    _state = State::Text;
                break;
        }
    }
}
