#pragma once

// TJpgDec for the cover viewer: FreeInkBook's vendored decoder (8-bit grayscale
// output, see its tjpgdcnf.h), not the ESP32 ROM one. FreeInkBook links that
// decoder and its definitions override the ROM's weak (PROVIDE) symbols, so the
// viewer has to compile against the same header and struct layout.

// Names a FreeInkBook header so PlatformIO's dependency finder links the library
// (and with it the decoder) into every build that has the cover viewer.
#include <BookProfile.h>

#include <cstddef>
#include <cstdint>
typedef size_t UINT;
typedef uint8_t BYTE;

#include "../../freeink-sdk/libs/book/FreeInkBook/third_party/tjpgd/tjpgd.h"

// The vendored jd_decomp takes an int-returning output callback.
#define jd_decomp(jd, outfunc, scale) jd_decomp((jd), reinterpret_cast<int (*)(JDEC*, void*, JRECT*)>(outfunc), (scale))
