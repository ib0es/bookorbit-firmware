#pragma once
// MOBI (Mobipocket 6, also old .azw / .prc) -> EPUB 2, streamed. The text
// records are decompressed (PalmDOC) one at a time and their HTML, which is
// loose (unquoted attributes, unclosed tags, <mbp:pagebreak/>, filepos links,
// recindex images), is rewritten as XHTML: twice, first to find link targets,
// file boundaries and the contents, then for the output. Images are copied
// from their records. KF8 (AZW3), HUFF/CDIC compression and DRM are refused
// with a reason in stats.error.
#include "convert_io.h"
#include "fb2_to_epub.h"   // ConvertResult, ConvertStats

namespace convert {

// What a PalmDB book holds, from its headers alone (for choosing a converter).
struct MobiInfo {
    bool isMobi = false;      // a BOOKMOBI / TEXtREAd database
    bool kf8Only = false;     // KF8 (AZW3) with no MOBI 6 part
    bool hasKf8 = false;
    bool drm = false;
    uint16_t compression = 0; // 1 none, 2 PalmDOC, 17480 HUFF/CDIC
};
MobiInfo probeMobi(InFile& in);

ConvertResult mobiToEpub(InFile& in, OutFile& out, TempFile& tmp, ProgressFn progress = nullptr, void* user = nullptr,
                         ConvertStats* stats = nullptr);

}  // namespace convert
