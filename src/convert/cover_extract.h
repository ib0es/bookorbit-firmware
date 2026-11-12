#pragma once
// Fast cover-only extraction from FB2 and MOBI/AZW3/AZW without full EPUB conversion.
#include <Arduino.h>
#include "convert_io.h"

namespace convert {

// Stream-level extractors usable without SD/Storage (e.g. host unit tests).
bool extractFb2Cover(InFile& in, OutFile& out);
bool extractMobiCover(InFile& in, OutFile& out);

// Extracts an embedded cover from srcPath (.fb2, .mobi, .azw3, .azw) to dstPath
// atomically via a temporary file. Returns true if a non-empty image was written.
bool extractDirectCover(const String& srcPath, const String& dstPath);

}  // namespace convert
