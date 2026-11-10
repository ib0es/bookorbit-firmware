#pragma once

#include <Arduino.h>
#include "tjpgd_compat.h"
#include "../hal/display.h"
#include "../hal/storage.h"

enum CoverFitMode {
    COVER_FIT_SCALE = 0, // Fit within bounds, preserve aspect ratio, letterbox
    COVER_FIT_CROP  = 1  // Fill bounds completely, preserve aspect ratio, crop edges
};

class JpegViewer {
public:
    // Unified cover rendering: loads BMP instantly if available, otherwise decodes JPEG and caches BMP
    static bool drawCover(const char* filePath, int x, int y, int maxW, int maxH, CoverFitMode fit = COVER_FIT_SCALE);

    // Fast direct BMP stream renderer (1-bit, 8-bit, 24-bit/32-bit BMP)
    static bool drawBmp(const char* filePath, int x, int y, int maxW, int maxH, CoverFitMode fit = COVER_FIT_SCALE);

    // JPEG decoder with Bayer 4x4 dithering
    static bool drawJpeg(const char* filePath, int x, int y, int maxW, int maxH, CoverFitMode fit = COVER_FIT_SCALE);

    // Typographic fallback placeholder when no cover is available
    static void drawCoverPlaceholder(const char* title, const char* author, int x, int y, int w, int h);

    // Saves a 1-bit monochrome image as a standard Windows BMP file
    static bool save1BitBmp(const char* filePath, int w, int h, const uint8_t* buffer);

    static void clearCache();
    static bool hasCached(const char* filePath, int maxW, int maxH, CoverFitMode fit = COVER_FIT_SCALE);
};
