#pragma once
// Book conversion on the card: the converters' files over SdFat, and the
// "converted copy" of a book kept in its cache directory.
#include <Arduino.h>

#include "convert_io.h"

namespace convert {

// Bump to have every converted book made again (a converter fix).
constexpr int kConverterVersion = 6;

// True for the formats read through a converted EPUB (FB2 for now).
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
