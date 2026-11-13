#include "epub_cfi.h"

#include <cctype>
#include <cstdlib>

namespace EpubCfi {

namespace {

// Reads "/N[assertion]" steps from s[pos..end); stops at ':', ',', ')', '!' or the end.
bool readSteps(const std::string& s, size_t& pos, size_t end, std::vector<int>& steps) {
    while (pos < end && s[pos] == '/') {
        pos++;
        if (pos >= end || !isdigit((unsigned char)s[pos])) return false;
        int v = 0;
        while (pos < end && isdigit((unsigned char)s[pos])) v = v * 10 + (s[pos++] - '0');
        steps.push_back(v);
        if (pos < end && s[pos] == '[') {           // id assertion
            size_t close = s.find(']', pos);
            if (close == std::string::npos || close >= end) return false;
            pos = close + 1;
        }
    }
    return true;
}

bool readOffset(const std::string& s, size_t& pos, size_t end, int& offset) {
    if (pos >= end || s[pos] != ':') return true;
    pos++;
    if (pos >= end || !isdigit((unsigned char)s[pos])) return false;
    offset = 0;
    while (pos < end && isdigit((unsigned char)s[pos])) offset = offset * 10 + (s[pos++] - '0');
    return true;
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Local name of a tag body ("/html:p class=..." -> "p"), lower case.
std::string tagName(const std::string& tag) {
    size_t i = (!tag.empty() && tag[0] == '/') ? 1 : 0;
    size_t start = i;
    while (i < tag.size() && !isSpace(tag[i]) && tag[i] != '/' && tag[i] != '>') i++;
    std::string name = tag.substr(start, i - start);
    size_t colon = name.rfind(':');
    if (colon != std::string::npos) name = name.substr(colon + 1);
    for (auto& c : name) c = (char)tolower((unsigned char)c);
    return name;
}

}  // namespace

bool parse(const std::string& cfi, Location& out) {
    out = Location();
    const std::string prefix = "epubcfi(";
    if (cfi.compare(0, prefix.size(), prefix) != 0) return false;
    const size_t end = cfi.rfind(')');
    if (end == std::string::npos || end < prefix.size()) return false;

    size_t pos = prefix.size();
    std::vector<int> pkg;
    if (!readSteps(cfi, pos, end, pkg) || pkg.size() < 2 || pos >= end || cfi[pos] != '!') return false;
    if (pkg[1] <= 0 || pkg[1] % 2) return false;
    out.ordinal = pkg[1] / 2;
    pos++;

    if (!readSteps(cfi, pos, end, out.steps)) return false;
    if (pos < end && cfi[pos] == ',') {               // range: parent path, start, end
        pos++;
        if (!readSteps(cfi, pos, end, out.steps)) return false;
    }
    if (!readOffset(cfi, pos, end, out.offset)) return false;
    return !out.steps.empty();
}

std::string format(const Location& loc) {
    std::string s = "epubcfi(/6/" + std::to_string(loc.ordinal * 2) + "!";
    for (int step : loc.steps) s += "/" + std::to_string(step);
    if (!loc.steps.empty() && loc.steps.back() % 2) s += ":" + std::to_string(loc.offset);
    return s + ")";
}

Walker Walker::toOffset(TextSink& sink, const std::vector<int>& steps, int offset) {
    Walker w(sink, Mode::ToOffset);
    w._targetSteps = steps;
    w._targetOffset = offset;
    return w;
}

Walker Walker::toLocation(TextSink& sink, uint32_t target) {
    Walker w(sink, Mode::ToLocation);
    w._targetText = target;
    return w;
}

void Walker::stop(bool reached) {
    _done = true;
    _reached = reached;
    _result = _sink.emitted();
}

void Walker::finish() {
    if (!_done) stop(false);
}

int Walker::compareToTarget(const std::vector<int>& node) const {
    const std::vector<int>& t = _targetSteps;
    const size_t n = node.size() < t.size() ? node.size() : t.size();
    for (size_t i = 0; i < n; i++) {
        if (node[i] != t[i]) return node[i] < t[i] ? -1 : 1;
    }
    if (node.size() == t.size()) return 0;
    return node.size() < t.size() ? -1 : 1;   // ancestor: before; descendant: after the start
}

std::vector<int> Walker::textNodePath() const {
    std::vector<int> p = _path;
    p.push_back(2 * _childElems.back() + 1);
    return p;
}

bool Walker::textCharStarts(bool whitespace) {
    if (!_inRoot || _inHead || _childElems.empty()) return false;
    const int index = 2 * _childElems.back() + 1;
    if (index != _textNodeIndex) {   // first character of a new text chunk
        _textNodeIndex = index;
        _textChars = 0;
    }
    if (_mode == Mode::ToOffset) {
        const int c = compareToTarget(textNodePath());
        if (c > 0 || (c == 0 && _textChars >= _targetOffset)) {
            stop(true);
            return true;
        }
    } else if (!whitespace && _sink.emitted() >= _targetText) {
        _loc.steps = textNodePath();
        _loc.offset = _textChars;
        stop(true);
        return true;
    }
    return false;
}

void Walker::onTagEnd() {
    const std::string& tag = _tag;   // e.g. "p class=\"x\">" or "/div>" or "br/>"
    if (tag.empty() || tag[0] == '?' || tag[0] == '!') return;
    const bool closing = tag[0] == '/';
    const bool selfClosing = tag.size() >= 2 && tag[tag.size() - 2] == '/';
    const std::string name = tagName(tag);
    _textNodeIndex = -1;   // text after a tag is a new chunk

    if (!_inRoot) {
        if (!closing && name == "html") {
            _inRoot = true;
            _childElems.assign(1, 0);
            _path.clear();
        }
        return;
    }
    if (closing) {
        if (name == "head") _inHead = false;
        if (_childElems.size() > 1) {
            _childElems.pop_back();
            _path.pop_back();
        } else {
            _inRoot = false;   // </html>
        }
        return;
    }
    const int index = 2 * (++_childElems.back());
    std::vector<int> elementPath = _path;
    elementPath.push_back(index);
    if (name == "head" && !selfClosing) _inHead = true;
    if (_mode == Mode::ToOffset && compareToTarget(elementPath) >= 0) {
        stop(true);   // at the target element (after its start tag), or past a missing target
        return;
    }
    if (!selfClosing) {
        _path = elementPath;
        _childElems.push_back(0);
    }
}

void Walker::feed(const char* data, size_t len) {
    for (size_t i = 0; i < len && !_done; i++) {
        const char c = data[i];
        switch (_state) {
            case State::Text:
                if (c == '<') {
                    _state = State::Tag;
                    _tag.clear();
                    _utf8Pending = 0;
                } else if (_utf8Pending > 0) {
                    _utf8Pending--;
                } else {
                    // A character starts here: decide before handing it to the sink.
                    if (textCharStarts(isSpace(c))) return;
                    const unsigned char u = (unsigned char)c;
                    int units = 1;
                    if (c == '&')
                        _state = State::Entity;
                    else if (u >= 0xF0) {
                        _utf8Pending = 3;
                        units = 2;
                    }   // outside the BMP: a UTF-16 pair
                    else if (u >= 0xE0)
                        _utf8Pending = 2;
                    else if (u >= 0xC0)
                        _utf8Pending = 1;
                    _textChars += units;
                }
                break;
            case State::Entity:
                if (c == ';' || c == '<' || isSpace(c)) _state = State::Text;
                if (c == '<') {
                    _state = State::Tag;
                    _tag.clear();
                }
                break;
            case State::Tag: {
                _tag += c;
                if (_tag == "!--") {
                    _state = State::Comment;
                    _special.clear();
                    break;
                }
                if (_tag == "![CDATA[") {
                    _state = State::CData;
                    _special.clear();
                    break;
                }
                if (c == '>') {
                    // '>' inside a quoted attribute value does not end the tag.
                    char quote = 0;
                    for (char t : _tag) {
                        if (quote) {
                            if (t == quote) quote = 0;
                        } else if (t == '"' || t == '\'')
                            quote = t;
                    }
                    if (!quote) {
                        _sink.feed(&c, 1);
                        _state = State::Text;
                        onTagEnd();
                        continue;
                    }
                }
                break;
            }
            case State::Comment:
            case State::CData: {
                _special += c;
                if (_special.size() > 3) _special.erase(0, _special.size() - 3);
                if ((_state == State::Comment && _special == "-->") || (_state == State::CData && _special == "]]>")) {
                    _state = State::Text;
                }
                break;
            }
        }
        _sink.feed(&c, 1);
    }
}

}  // namespace EpubCfi
