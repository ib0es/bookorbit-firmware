#pragma once

// The cover viewer uses its own saturating grayscale decoder implementation.
// FreeInkBook's decoder, configuration and public symbols stay unchanged.
#include <cstddef>
#include <cstdint>
typedef size_t UINT;
typedef uint8_t BYTE;

#define jd_prepare bookorbit_cover_jd_prepare
#define jd_decomp bookorbit_cover_jd_decomp
#include "../../freeink-sdk/libs/book/FreeInkBook/third_party/tjpgd/tjpgd.h"
#undef jd_decomp
#define jd_decomp(jd, outfunc, scale) bookorbit_cover_jd_decomp((jd), reinterpret_cast<int (*)(JDEC*, void*, JRECT*)>(outfunc), (scale))
