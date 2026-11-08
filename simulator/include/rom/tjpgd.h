#pragma once

#include <cstdint>
#include <cstddef>
typedef size_t UINT;
typedef uint8_t BYTE;

#include "../../../../freeink-sdk/libs/book/FreeInkBook/third_party/tjpgd/tjpgd.h"

#define jd_decomp(jd, outfunc, scale) jd_decomp((jd), reinterpret_cast<int (*)(JDEC*, void*, JRECT*)>(outfunc), (scale))
