#pragma once
// The TrueType family built into the firmware (tools/gen_ttf_fonts.py).
#include <stdint.h>

namespace builtinfont {

const char* name();
// The file of a face: style 0 regular, 1 bold, 2 italic, 3 bold italic.
const uint8_t* face(int style, uint32_t* len);

}  // namespace builtinfont
