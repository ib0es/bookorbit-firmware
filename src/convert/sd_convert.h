#pragma once
// Book conversion on the card: the converters' files over SdFat, and the
// "converted copy" of a book kept in its cache directory.
#include <Arduino.h>

#include "convert_io.h"

namespace convert {

// Bump to have every converted book made again (a converter fix).
constexpr int kConverterVersion = 6;
// FB2 books on their own (7: laid out as the web reader does, with a CFI map).
constexpr int kFb2ConverterVersion = 7;
// AZW3/KF8 books on their own (7: their markup kept as it is, with a CFI map).
constexpr int kKf8ConverterVersion = 7;

// In the cache directory of a converted FB2: its CFI map (core/cfi_map.h).
constexpr const char* kCfiMapName = "cfimap.txt";

// True for the formats read through a converted EPUB: FB2, TXT, MOBI/AZW/AZW3.
bool needsConversion(const String& path);

// The EPUB to read for `path`: converted into `cacheDir` on first use (and
// again when the source changed or the converter did). Draws its progress
// through `progress`. `noMemory` is set when it failed for lack of memory.
// `why` (optional) says what failed: a protected (DRM) book, a format variant not
// supported (HUFF/CDIC), unreadable data.
enum class ConvertProblem : uint8_t { None, NoMemory, Drm, Unsupported, Broken };
bool ensureConverted(const String& path, const String& cacheDir, String& epubPath, ProgressFn progress,
                     void* user, bool* noMemory = nullptr, ConvertProblem* why = nullptr);

}  // namespace convert
