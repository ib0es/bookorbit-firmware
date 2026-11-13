#pragma once

#include <Arduino.h>

// One table-of-contents entry as the reader menus show it.
struct ChapterItem {
    String title;
    uint32_t byteOffset = 0;   // SimpleReader: offset in the converted text; BookReader: spine index
    int page = 0;              // first page (BookReader: an estimate until the chapter is laid out)
};
