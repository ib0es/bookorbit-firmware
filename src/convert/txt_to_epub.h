#pragma once
// Plain text -> EPUB 2, streamed in one pass over the text (plus a look at its
// first 64 KB). The encoding is found from a BOM, else by whether the text is
// valid UTF-8, else by which Cyrillic code page (windows-1251, KOI8-R, CP866)
// gives the most lowercase letters, else windows-1252. How paragraphs are
// marked is found the same way: one per line, separated by blank lines (hard-
// wrapped text), or started by an indent. Lines like "Глава 3", "CHAPTER IV.",
// "Часть первая", "Пролог" or a lone roman numeral become headings, chapter
// files and the table of contents; a run of them with no text between (a
// contents list) stays text. RAM: a few KB of buffers plus the contents.
#include <string>

#include "convert_io.h"
#include "fb2_to_epub.h"   // ConvertResult, ConvertStats

namespace convert {

enum class TextEncoding : uint8_t { Utf8, Utf16LE, Utf16BE, Cp1251, Koi8R, Cp866, Cp1252 };
const char* encodingName(TextEncoding e);

// From the first bytes of a text (a BOM, else statistics); `bomBytes` is how many to skip.
TextEncoding detectEncoding(const uint8_t* data, uint32_t len, uint32_t* bomBytes);

// `title` is the book's name (the file has none of its own): its file name, say.
ConvertResult txtToEpub(InFile& in, OutFile& out, const std::string& title, ProgressFn progress = nullptr,
                        void* user = nullptr, ConvertStats* stats = nullptr);

}  // namespace convert
