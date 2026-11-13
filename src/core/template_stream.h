#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace TemplateStream {
// Fixed working memory regardless of template size. Sink consumes synchronously.
template <typename Sink, size_t Capacity = 512>
class Buffer {
public:
    explicit Buffer(Sink sink) : sink_(sink) {}
    void append(const char* text, size_t length) {
        while (length) {
            const size_t take = std::min(length, Capacity - used_);
            memcpy(bytes_ + used_, text, take);
            used_ += take;
            text += take;
            length -= take;
            if (used_ == Capacity) flush();
        }
    }
    void flush() {
        if (used_) {
            sink_(bytes_, used_);
            used_ = 0;
        }
    }

private:
    Sink sink_;
    char bytes_[Capacity]{};
    size_t used_ = 0;
};

// Only %UPPER_CASE_TOKENS% are placeholders; JavaScript modulo operators and
// unknown tokens are passed through without changing their bytes.
template <typename Visitor>
void visit(const char* text, size_t length, Visitor visitor) {
    size_t literal = 0, cursor = 0;
    while (cursor < length) {
        if (text[cursor] != '%') {
            cursor++;
            continue;
        }
        size_t end = cursor + 1;
        while (end < length && end - cursor <= 64 &&
               ((text[end] >= 'A' && text[end] <= 'Z') || (text[end] >= '0' && text[end] <= '9') || text[end] == '_'))
            end++;
        if (end == cursor + 1 || end >= length || end - cursor > 64 || text[end] != '%') {
            cursor++;
            continue;
        }
        if (cursor > literal) visitor(text + literal, cursor - literal, false);
        visitor(text + cursor, end - cursor + 1, true);
        cursor = end + 1;
        literal = cursor;
    }
    if (literal < length) visitor(text + literal, length - literal, false);
}
} // namespace TemplateStream
