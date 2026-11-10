// The built-in TrueType family's files, kept in flash (read in place).
#include "builtin_font.h"

#include "builtin_font_data.h"

namespace builtinfont {

const char* name() { return kName; }

const uint8_t* face(int style, uint32_t* len) {
    switch (style & 3) {
        case 0: *len = k_regular_size; return k_regular;
        case 1: *len = k_bold_size; return k_bold;
        case 2: *len = k_italic_size; return k_italic;
        default: *len = k_bold_italic_size; return k_bold_italic;
    }
}

}  // namespace builtinfont
