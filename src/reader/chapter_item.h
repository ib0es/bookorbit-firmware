#pragma once

#include <Arduino.h>

// One table-of-contents entry as the reader menus show it.
struct ChapterItem {
    String title;
    uint32_t byteOffset;   // SimpleReader: offset in the converted text; BookReader: spine index
    int page;              // first page (BookReader: an estimate until the chapter is laid out)
};
