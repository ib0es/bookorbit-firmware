#pragma once
// FB2 (FictionBook 2) -> EPUB 2, streamed: the book is read twice (ids, images
// and paragraph style first, then the output), never held in RAM. Output is a
// stored-entry EPUB: chapters by top-level section (and by size, so that one
// chapter never grows past what the reader lays out quickly), images decoded
// from base64 to files, notes in their own chapters, an NCX table of contents.
// UTF-8, UTF-16 and windows-1251 sources. RAM: expat (~10-20 KB) plus a few
// small tables (6 bytes per id, 5 per image).
#include <string>

#include "convert_io.h"

namespace convert {

enum class ConvertResult : uint8_t { Ok, NotThisFormat, ParseError, IoError, NoMemory };
const char* resultName(ConvertResult r);

struct ConvertStats {
    uint32_t chapters = 0;
    uint32_t images = 0;
    uint32_t tocEntries = 0;
    uint32_t ids = 0;
    std::string title;
    std::string author;
    std::string lang;
    std::string error;   // what failed, for the log
};

ConvertResult fb2ToEpub(InFile& in, OutFile& out, TempFile& tmp, ProgressFn progress = nullptr,
                        void* user = nullptr, ConvertStats* stats = nullptr);

}  // namespace convert
