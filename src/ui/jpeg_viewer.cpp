#include "jpeg_viewer.h"
#include "kit/ui_kit.h"
#include "font.h"
#include <algorithm>
#include <math.h>
#include "../platform/device.h"

struct JpegContext {
    FsFile* file;
    int boxX;
    int boxY;
    int maxW;
    int maxH;
    int targetX;
    int targetY;
    int finalW;
    int finalH;
    int srcW;
    int srcH;
    uint8_t* cacheBuf;
    int cacheW;
    int cacheH;

    // Streaming Area-Average + Floyd-Steinberg error diffusion
    bool useFloyd;
    uint8_t* band;
    uint16_t bandTop;
    uint16_t bandFilled;
    uint16_t* colMap;
    uint32_t* accSum;
    uint16_t* accCnt;
    uint8_t* lastGray;
    int16_t* errCur;
    int16_t* errNext;
    int curDstY;
};

// Fast 1-bit RAM cache of cover thumbnails, so the home screen redraws without
// reading the card or decoding a JPEG. As many as the memory tier allows (one on
// the C3, the home carousel's on the S3); the least recently drawn one makes room.
static const int MAX_CACHE_W = 160;
static const int MAX_CACHE_H = 240;
static const int CACHE_ROW_BYTES = (MAX_CACHE_W / 8); // 20 bytes
static const int CACHE_BYTES = CACHE_ROW_BYTES * MAX_CACHE_H; // 4800 bytes

namespace {
struct CoverSlot {
    uint8_t* bits = nullptr;
    String path;
    int w = 0;
    int h = 0;
    CoverFitMode fit = COVER_FIT_SCALE;
    bool valid = false;
    uint32_t lastUse = 0;
};
constexpr int kCoverSlots = Platform::kMemory.coversInRam > 0 ? Platform::kMemory.coversInRam : 1;
uint8_t s_firstSlotBits[CACHE_BYTES];   // the first slot is static; more come from the heap (PSRAM on the S3)
CoverSlot s_slots[kCoverSlots];
uint32_t s_useClock = 0;

CoverSlot* findSlot(const char* filePath, int maxW, int maxH, CoverFitMode fit) {
    if (!filePath || !*filePath) return nullptr;
    for (CoverSlot& c : s_slots) {
        if (c.valid && c.w == maxW && c.h == maxH && c.fit == fit && c.path.equals(filePath)) return &c;
    }
    return nullptr;
}

// A slot to decode a new thumbnail into, cleared to white paper and not valid
// until commitSlot().
CoverSlot* takeSlot() {
    CoverSlot* pick = nullptr;
    for (int i = 0; i < kCoverSlots; i++) {
        CoverSlot& c = s_slots[i];
        if (!c.bits) c.bits = i == 0 ? s_firstSlotBits : (uint8_t*)malloc(CACHE_BYTES);
        if (!c.bits) continue;
        if (!c.valid) { pick = &c; break; }
        if (!pick || c.lastUse < pick->lastUse) pick = &c;
    }
    pick->valid = false;
    memset(pick->bits, 0, CACHE_BYTES);   // 0 = white paper
    return pick;
}

void commitSlot(CoverSlot* c, const char* filePath, int maxW, int maxH, CoverFitMode fit) {
    c->path = filePath;
    c->w = maxW;
    c->h = maxH;
    c->fit = fit;
    c->valid = true;
    c->lastUse = ++s_useClock;
}
}

void JpegViewer::clearCache() {
    for (CoverSlot& c : s_slots) {
        c.valid = false;
        c.path = "";
    }
}

bool JpegViewer::hasCached(const char* filePath, int maxW, int maxH, CoverFitMode fit) {
    return findSlot(filePath, maxW, maxH, fit) != nullptr;
}

// Input function for TJpgDec
static UINT tjpgdInput(JDEC* jd, BYTE* buff, UINT ndata) {
    JpegContext* ctx = (JpegContext*)jd->device;
    if (!ctx || !ctx->file || !*(ctx->file)) return 0;

    if (buff) {
        return ctx->file->read(buff, ndata);
    } else {
        // Skip ndata bytes
        return ctx->file->seek(ctx->file->curPosition() + ndata) ? ndata : 0;
    }
}

// 8x8 Bayer Dithering Matrix (64 levels of gray) for smooth fallback ordered dithering
static const uint8_t bayer8x8[8][8] = {
    {   2, 130,  34, 162,  10, 138,  42, 170 },
    { 194,  66, 226,  98, 202,  74, 234, 106 },
    {  50, 178,  18, 146,  58, 186,  26, 154 },
    { 242, 114, 210,  82, 250, 122, 218,  90 },
    {  14, 142,  46, 174,   6, 134,  38, 166 },
    { 206,  78, 238, 110, 198,  70, 230, 102 },
    {  62, 190,  30, 158,  54, 182,  22, 150 },
    { 254, 126, 222,  94, 246, 118, 214,  86 }
};

static void finalizeDstRow(JpegContext* ctx) {
    if (!ctx || ctx->curDstY < 0 || ctx->curDstY >= ctx->finalH) return;

    int py = ctx->targetY + ctx->curDstY;
    bool inBoxY = (py >= ctx->boxY && py < ctx->boxY + ctx->maxH);

    uint8_t left = ctx->lastGray[0];
    for (int dx = 0; dx < ctx->finalW; dx++) {
        if (ctx->accCnt[dx] > 0) {
            left = (uint8_t)(ctx->accSum[dx] / ctx->accCnt[dx]);
            ctx->lastGray[dx] = left;
        } else {
            left = ctx->lastGray[dx];
        }

        int v = left + ctx->errCur[dx + 1];
        if (v < 0) v = 0;
        if (v > 255) v = 255;

        EPDColor color = (v > 127) ? EPD_WHITE : EPD_BLACK;
        int err = v - (color == EPD_WHITE ? 255 : 0);

        ctx->errCur[dx + 2] += err * 7 / 16;
        ctx->errNext[dx]     += err * 3 / 16;
        ctx->errNext[dx + 1] += err * 5 / 16;
        ctx->errNext[dx + 2] += err * 1 / 16;

        int px = ctx->targetX + dx;
        if (inBoxY && px >= ctx->boxX && px < ctx->boxX + ctx->maxW) {
            Display.drawPixel(px, py, color);

            if (ctx->cacheBuf) {
                int localX = px - ctx->boxX;
                int localY = py - ctx->boxY;
                if (localX >= 0 && localX < ctx->cacheW && localY >= 0 && localY < ctx->cacheH) {
                    int wb = (ctx->cacheW + 7) / 8;
                    int byteIdx = localY * wb + (localX / 8);
                    int bitIdx = 7 - (localX % 8);
                    if (color == EPD_BLACK) {
                        ctx->cacheBuf[byteIdx] |= (1 << bitIdx);
                    }
                }
            }
        }
    }

    memcpy(ctx->errCur, ctx->errNext, (ctx->finalW + 2) * sizeof(int16_t));
    memset(ctx->errNext, 0, (ctx->finalW + 2) * sizeof(int16_t));

    memset(ctx->accSum, 0, ctx->finalW * sizeof(uint32_t));
    memset(ctx->accCnt, 0, ctx->finalW * sizeof(uint16_t));
}

static void flushBandRows(JpegContext* ctx, int upToSrcY) {
    if (!ctx) return;
    if (upToSrcY > ctx->srcH) upToSrcY = ctx->srcH;
    while (ctx->bandTop < upToSrcY) {
        int srcY = ctx->bandTop;
        const uint8_t* srcRow = ctx->band + (srcY % 16) * ctx->srcW;

        int dstY = (int)((int64_t)srcY * ctx->finalH / ctx->srcH);
        if (dstY >= ctx->finalH) dstY = ctx->finalH - 1;

        while (ctx->curDstY < dstY) {
            finalizeDstRow(ctx);
            ctx->curDstY++;
        }

        for (int sx = 0; sx < ctx->srcW; ++sx) {
            int dx = ctx->colMap[sx];
            ctx->accSum[dx] += srcRow[sx];
            ctx->accCnt[dx]++;
        }

        ctx->bandTop++;
    }
}

// Output function for TJpgDec
static UINT tjpgdOutput(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegContext* ctx = (JpegContext*)jd->device;
    if (!ctx || !bitmap || !rect) return 0;

    if (ctx->useFloyd) {
        if ((uint32_t)rect->top >= ctx->bandFilled) {
            flushBandRows(ctx, ctx->bandFilled);
            ctx->bandFilled = rect->bottom + 1;
        }

        int bw = rect->right - rect->left + 1;
        const uint8_t* src = (const uint8_t*)bitmap;
        for (int y = rect->top; y <= rect->bottom; ++y) {
            if (y >= ctx->srcH) break;
            uint8_t* dst = ctx->band + (y % 16) * ctx->srcW;
            for (int x = 0; x < bw; ++x) {
                int sx = rect->left + x;
                if (sx >= ctx->srcW) break;
#if defined(JD_FORMAT) && JD_FORMAT == 2
                dst[sx] = src[x];
#elif defined(JD_FORMAT) && JD_FORMAT == 1
                uint16_t pix16 = ((const uint16_t*)src)[x];
                uint8_t r = (pix16 >> 11) << 3;
                uint8_t g = ((pix16 >> 5) & 0x3F) << 2;
                uint8_t b = (pix16 & 0x1F) << 3;
                dst[sx] = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
#else
                const uint8_t* p = src + x * 3;
                dst[sx] = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
#endif
            }
#if defined(JD_FORMAT) && JD_FORMAT == 2
            src += bw;
#elif defined(JD_FORMAT) && JD_FORMAT == 1
            src += bw * 2;
#else
            src += bw * 3;
#endif
        }
        return 1;
    }

    // Fallback path: 8x8 Bayer ordered dithering
    int bw = rect->right - rect->left + 1;

    int dLeft = (int32_t)rect->left * ctx->finalW / ctx->srcW;
    int dRight = (int32_t)(rect->right + 1) * ctx->finalW / ctx->srcW + 1;
    if (dRight >= ctx->finalW) dRight = ctx->finalW - 1;

    int dTop = (int32_t)rect->top * ctx->finalH / ctx->srcH;
    int dBottom = (int32_t)(rect->bottom + 1) * ctx->finalH / ctx->srcH + 1;
    if (dBottom >= ctx->finalH) dBottom = ctx->finalH - 1;

    for (int dy = dTop; dy <= dBottom; dy++) {
        int sy = (int32_t)dy * ctx->srcH / ctx->finalH;
        if (sy < rect->top || sy > rect->bottom) continue;

        int py = ctx->targetY + dy;
        if (py < ctx->boxY || py >= ctx->boxY + ctx->maxH) continue;

        int rowOffset = (sy - rect->top) * bw;

        for (int dx = dLeft; dx <= dRight; dx++) {
            int sx = (int32_t)dx * ctx->srcW / ctx->finalW;
            if (sx < rect->left || sx > rect->right) continue;

            int px = ctx->targetX + dx;
            if (px < ctx->boxX || px >= ctx->boxX + ctx->maxW) continue;

            int pixelIdx = rowOffset + (sx - rect->left);

#if defined(JD_FORMAT) && JD_FORMAT == 2
            uint8_t gray = ((const uint8_t*)bitmap)[pixelIdx];
#elif defined(JD_FORMAT) && JD_FORMAT == 1
            uint16_t pix16 = ((const uint16_t*)bitmap)[pixelIdx];
            uint8_t r = (pix16 >> 11) << 3;
            uint8_t g = ((pix16 >> 5) & 0x3F) << 2;
            uint8_t b = (pix16 & 0x1F) << 3;
            uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
#else
            const uint8_t* p = (const uint8_t*)bitmap + pixelIdx * 3;
            uint8_t gray = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
#endif

            uint8_t threshold = bayer8x8[py & 7][px & 7];
            EPDColor color = (gray > threshold) ? EPD_WHITE : EPD_BLACK;

            Display.drawPixel(px, py, color);

            if (ctx->cacheBuf) {
                int localX = px - ctx->boxX;
                int localY = py - ctx->boxY;
                if (localX >= 0 && localX < ctx->cacheW && localY >= 0 && localY < ctx->cacheH) {
                    int wb = (ctx->cacheW + 7) / 8;
                    int byteIdx = localY * wb + (localX / 8);
                    int bitIdx = 7 - (localX % 8);
                    if (color == EPD_BLACK) {
                        ctx->cacheBuf[byteIdx] |= (1 << bitIdx);
                    }
                }
            }
        }
    }

    return 1;
}

bool JpegViewer::save1BitBmp(const char* filePath, int w, int h, const uint8_t* buffer) {
    if (!filePath || w <= 0 || h <= 0 || !buffer) return false;
    FsFile f = Storage.open(filePath, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) return false;

    int rowBytes = (w + 7) / 8;
    int bmpStride = ((w + 31) / 32) * 4;
    uint32_t imageSize = bmpStride * h;
    uint32_t fileSize = 62 + imageSize;

    uint8_t header[62] = {0};
    header[0] = 'B';
    header[1] = 'M';
    header[2] = (uint8_t)(fileSize);
    header[3] = (uint8_t)(fileSize >> 8);
    header[4] = (uint8_t)(fileSize >> 16);
    header[5] = (uint8_t)(fileSize >> 24);
    header[6] = 'F'; // BookOrbit Floyd-Steinberg quality signature
    header[7] = 'S';
    header[10] = 62; // Offset to pixel data

    header[14] = 40; // DIB header size
    header[18] = (uint8_t)(w);
    header[19] = (uint8_t)(w >> 8);
    header[20] = (uint8_t)(w >> 16);
    header[21] = (uint8_t)(w >> 24);
    header[22] = (uint8_t)(h);
    header[23] = (uint8_t)(h >> 8);
    header[24] = (uint8_t)(h >> 16);
    header[25] = (uint8_t)(h >> 24);
    header[26] = 1;  // Planes = 1
    header[28] = 1;  // Bit count = 1
    header[34] = (uint8_t)(imageSize);
    header[35] = (uint8_t)(imageSize >> 8);
    header[36] = (uint8_t)(imageSize >> 16);
    header[37] = (uint8_t)(imageSize >> 24);
    header[46] = 2;  // Colors used = 2
    header[50] = 2;  // Colors important = 2

    // Color 0 = White (paper background), Color 1 = Black (ink)
    // Since in our cache buffer bit 1 = black ink, map color table:
    // Index 0: White (0xFF, 0xFF, 0xFF)
    // Index 1: Black (0x00, 0x00, 0x00)
    header[54] = 0xFF;
    header[55] = 0xFF;
    header[56] = 0xFF;
    header[57] = 0x00;

    header[58] = 0x00;
    header[59] = 0x00;
    header[60] = 0x00;
    header[61] = 0x00;

    f.write(header, 62);

    uint8_t rowPad[4] = {0, 0, 0, 0};
    int padBytes = bmpStride - rowBytes;

    for (int y = h - 1; y >= 0; y--) {
        const uint8_t* rowSrc = buffer + y * rowBytes;
        f.write(rowSrc, rowBytes);
        if (padBytes > 0) {
            f.write(rowPad, padBytes);
        }
    }

    f.close();
    return true;
}

bool JpegViewer::drawBmp(const char* filePath, int x, int y, int maxW, int maxH, CoverFitMode fit) {
    if (!filePath || strlen(filePath) == 0 || !Storage.isAvailable() || !Storage.exists(filePath)) return false;
    FsFile f = Storage.open(filePath, O_RDONLY);
    if (!f) return false;

    uint8_t header[54];
    if (f.read(header, 54) != 54 || header[0] != 'B' || header[1] != 'M') {
        f.close();
        return false;
    }

    uint32_t offset = header[10] | (header[11] << 8) | (header[12] << 16) | (header[13] << 24);
    int32_t imgW = (int32_t)(header[18] | (header[19] << 8) | (header[20] << 16) | (header[21] << 24));
    int32_t imgH = (int32_t)(header[22] | (header[23] << 8) | (header[24] << 16) | (header[25] << 24));
    uint16_t bpp = header[28] | (header[29] << 8);

    if (imgW <= 0 || imgH == 0 || (bpp != 1 && bpp != 8 && bpp != 24 && bpp != 32)) {
        f.close();
        return false;
    }

    bool topDown = (imgH < 0);
    int h = topDown ? -imgH : imgH;
    int w = imgW;

    // Palette for 1-bit BMP
    uint8_t pal[2] = { 255, 0 }; // Default: 0 = white, 1 = black
    if (bpp == 1 && offset >= 62) {
        uint8_t palData[8];
        f.seek(54);
        if (f.read(palData, 8) == 8) {
            pal[0] = (uint8_t)(((int)palData[0] + palData[1] + palData[2]) / 3);
            pal[1] = (uint8_t)(((int)palData[4] + palData[5] + palData[6]) / 3);
        }
    }

    int rowStride = 0;
    if (bpp == 1) rowStride = ((w + 31) / 32) * 4;
    else if (bpp == 8) rowStride = ((w + 3) / 4) * 4;
    else if (bpp == 24) rowStride = ((w * 3 + 3) / 4) * 4;
    else if (bpp == 32) rowStride = w * 4;

    uint8_t* lineBuf = (uint8_t*)malloc(rowStride);
    if (!lineBuf) {
        f.close();
        return false;
    }

    bool isThumbnail = (maxW <= MAX_CACHE_W && maxH <= MAX_CACHE_H);
    CoverSlot* slot = isThumbnail ? takeSlot() : nullptr;
    uint8_t* thumbBits = slot ? slot->bits : nullptr;

    // Fast path: dimensions already match maxW x maxH (e.g. pre-rendered cache)
    if (w == maxW && h == maxH) {
        for (int row = 0; row < h; row++) {
            int py = y + row;
            int srcRow = topDown ? row : (h - 1 - row);
            f.seek(offset + (uint32_t)srcRow * rowStride);
            if (f.read(lineBuf, rowStride) != (size_t)rowStride) break;

            int wb = (maxW + 7) / 8;
            if (bpp == 1) {
                for (int col = 0; col < w; col++) {
                    int px = x + col;
                    uint8_t bit = (lineBuf[col / 8] >> (7 - (col % 8))) & 1;
                    EPDColor c = (pal[bit] > 128) ? EPD_WHITE : EPD_BLACK;
                    Display.drawPixel(px, py, c);
                    if (isThumbnail && c == EPD_BLACK) {
                        thumbBits[row * wb + (col / 8)] |= (1 << (7 - (col % 8)));
                    }
                }
            } else if (bpp == 8) {
                for (int col = 0; col < w; col++) {
                    int px = x + col;
                    uint8_t gray = lineBuf[col];
                    uint8_t threshold = bayer8x8[py & 7][px & 7];
                    EPDColor c = (gray > threshold) ? EPD_WHITE : EPD_BLACK;
                    Display.drawPixel(px, py, c);
                    if (isThumbnail && c == EPD_BLACK) {
                        thumbBits[row * wb + (col / 8)] |= (1 << (7 - (col % 8)));
                    }
                }
            } else if (bpp == 24 || bpp == 32) {
                int step = (bpp == 32) ? 4 : 3;
                for (int col = 0; col < w; col++) {
                    int px = x + col;
                    uint8_t b = lineBuf[col * step];
                    uint8_t g = lineBuf[col * step + 1];
                    uint8_t r = lineBuf[col * step + 2];
                    uint8_t gray = (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
                    uint8_t threshold = bayer8x8[py & 7][px & 7];
                    EPDColor c = (gray > threshold) ? EPD_WHITE : EPD_BLACK;
                    Display.drawPixel(px, py, c);
                    if (isThumbnail && c == EPD_BLACK) {
                        thumbBits[row * wb + (col / 8)] |= (1 << (7 - (col % 8)));
                    }
                }
            }
        }
    } else {
        // Scaled path: arbitrary BMP resized with area averaging + Floyd-Steinberg error diffusion
        Display.fillRect(x, y, maxW, maxH, EPD_WHITE);

        float s = (fit == COVER_FIT_CROP)
            ? ((float)maxW / w > (float)maxH / h ? (float)maxW / w : (float)maxH / h)
            : ((float)maxW / w < (float)maxH / h ? (float)maxW / w : (float)maxH / h);
        int finalW = (int)round(w * s);
        int finalH = (int)round(h * s);
        if (finalW < 1) finalW = 1;
        if (finalH < 1) finalH = 1;

        int targetX = x + (maxW - finalW) / 2;
        int targetY = y + (maxH - finalH) / 2;

        int lastSrcRow = -1;
        int wb = (maxW + 7) / 8;

        int16_t* errCur = (int16_t*)calloc(finalW + 2, sizeof(int16_t));
        int16_t* errNext = (int16_t*)calloc(finalW + 2, sizeof(int16_t));

        for (int dy = 0; dy < finalH; dy++) {
            int py = targetY + dy;
            bool inBoxY = (py >= y && py < y + maxH);

            int sy = (int32_t)dy * h / finalH;
            int srcRow = topDown ? sy : (h - 1 - sy);
            if (srcRow != lastSrcRow) {
                f.seek(offset + (uint32_t)srcRow * rowStride);
                if (f.read(lineBuf, rowStride) != (size_t)rowStride) break;
                lastSrcRow = srcRow;
            }

            for (int dx = 0; dx < finalW; dx++) {
                int px = targetX + dx;
                int sx0 = (int32_t)dx * w / finalW;
                int sx1 = (int32_t)(dx + 1) * w / finalW;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sx1 > w) sx1 = w;

                uint32_t sumGray = 0;
                int count = 0;
                for (int sx = sx0; sx < sx1; sx++) {
                    uint8_t g = 255;
                    if (bpp == 1) {
                        uint8_t bit = (lineBuf[sx / 8] >> (7 - (sx % 8))) & 1;
                        g = pal[bit];
                    } else if (bpp == 8) {
                        g = lineBuf[sx];
                    } else if (bpp == 24 || bpp == 32) {
                        int step = (bpp == 32) ? 4 : 3;
                        uint8_t b = lineBuf[sx * step];
                        uint8_t g8 = lineBuf[sx * step + 1];
                        uint8_t r = lineBuf[sx * step + 2];
                        g = (uint8_t)((r * 77 + g8 * 150 + b * 29) >> 8);
                    }
                    sumGray += g;
                    count++;
                }
                uint8_t gray = count ? (uint8_t)(sumGray / count) : 255;

                EPDColor c;
                if (errCur && errNext) {
                    int v = gray + errCur[dx + 1];
                    if (v < 0) v = 0;
                    if (v > 255) v = 255;
                    c = (v > 127) ? EPD_WHITE : EPD_BLACK;
                    int err = v - (c == EPD_WHITE ? 255 : 0);
                    errCur[dx + 2] += err * 7 / 16;
                    errNext[dx]     += err * 3 / 16;
                    errNext[dx + 1] += err * 5 / 16;
                    errNext[dx + 2] += err * 1 / 16;
                } else {
                    uint8_t threshold = bayer8x8[py & 7][px & 7];
                    c = (gray > threshold) ? EPD_WHITE : EPD_BLACK;
                }

                if (inBoxY && px >= x && px < x + maxW) {
                    Display.drawPixel(px, py, c);

                    if (isThumbnail && c == EPD_BLACK) {
                        int localX = px - x;
                        int localY = py - y;
                        if (localX >= 0 && localX < maxW && localY >= 0 && localY < maxH) {
                            thumbBits[localY * wb + (localX / 8)] |= (1 << (7 - (localX % 8)));
                        }
                    }
                }
            }

            if (errCur && errNext) {
                memcpy(errCur, errNext, (finalW + 2) * sizeof(int16_t));
                memset(errNext, 0, (finalW + 2) * sizeof(int16_t));
            }
        }

        if (errCur) free(errCur);
        if (errNext) free(errNext);
    }

    if (slot) commitSlot(slot, filePath, maxW, maxH, fit);

    free(lineBuf);
    f.close();
    return true;
}

bool JpegViewer::drawCover(const char* filePath, int x, int y, int maxW, int maxH, CoverFitMode fit) {
    if (!filePath || strlen(filePath) == 0) return false;

    // 1. RAM cache check for rapid home screen redraws (< 0.5 ms)
    if (CoverSlot* cached = findSlot(filePath, maxW, maxH, fit)) {
        cached->lastUse = ++s_useClock;
        const uint8_t* bits = cached->bits;
        int wb = (maxW + 7) / 8;
        for (int row = 0; row < maxH; row++) {
            int py = y + row;
            int rowOffset = row * wb;
            for (int col = 0; col < maxW; col++) {
                int byteIdx = rowOffset + (col / 8);
                int bitIdx = 7 - (col % 8);
                bool isBlack = (bits[byteIdx] & (1 << bitIdx)) != 0;
                Display.drawPixel(x + col, py, isBlack ? EPD_BLACK : EPD_WHITE);
            }
        }
        return true;
    }

    String pathStr(filePath);

    // 2. Direct BMP rendering if file is already a BMP
    if (pathStr.endsWith(".bmp") || pathStr.endsWith(".BMP")) {
        return drawBmp(filePath, x, y, maxW, maxH, fit);
    }

    // 3. Check for pre-rendered BMP cache on SD card (CrossPoint style: instant loading without JPEG lag)
    int slash = pathStr.lastIndexOf('/');
    String base = (slash >= 0) ? pathStr.substring(slash + 1) : pathStr;
    int dot = base.lastIndexOf('.');
    if (dot > 0) base = base.substring(0, dot);

    String bmpCachePath = "";
    if (maxW <= 160 && maxH <= 240) {
        bmpCachePath = "/covers/" + base + ((fit == COVER_FIT_CROP) ? "_thumb_crop.bmp" : "_thumb_fit.bmp");
    } else {
        bmpCachePath = "/covers/" + base + ((fit == COVER_FIT_CROP) ? "_crop.bmp" : "_fit.bmp");
    }

    if (Storage.exists(bmpCachePath.c_str())) {
        FsFile bf = Storage.open(bmpCachePath.c_str(), O_RDONLY);
        if (bf && bf.size() > 500) {
            uint8_t hdr[26];
            bool sizeMatches = false;
            if (bf.read(hdr, 26) == 26 && hdr[0] == 'B' && hdr[1] == 'M') {
                int32_t cw = (int32_t)(hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | (hdr[21] << 24));
                int32_t ch = (int32_t)(hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | (hdr[25] << 24));
                if (ch < 0) ch = -ch;
                if (cw == maxW && ch == maxH && hdr[6] == 'F' && hdr[7] == 'S') {
                    sizeMatches = true;
                }
            }
            bf.close();
            if (sizeMatches) {
                return drawBmp(bmpCachePath.c_str(), x, y, maxW, maxH, fit);
            }
        } else if (bf) {
            bf.close();
        }
    }

    // 4. Decode JPEG cover, render to display, and save pre-rendered 1-bit BMP cache
    bool ok = drawJpeg(filePath, x, y, maxW, maxH, fit);
    if (!ok && Storage.exists(bmpCachePath.c_str())) {
        return drawBmp(bmpCachePath.c_str(), x, y, maxW, maxH, fit);
    }
    return ok;
}

bool JpegViewer::drawJpeg(const char* filePath, int x, int y, int maxW, int maxH, CoverFitMode fit) {
    if (!filePath || strlen(filePath) == 0) return false;

    if (!Storage.isAvailable() || !Storage.exists(filePath)) {
        return false;
    }

    uint32_t t0 = millis();
    FsFile f = Storage.open(filePath, O_RDONLY);
    if (!f) return false;

    // Clear target area to white before rendering
    Display.fillRect(x, y, maxW, maxH, EPD_WHITE);

    // Working memory pool for TJpgDec (high-res images need up to 8-16 KB)
    size_t poolSize = 16384;
    uint8_t* pool = (uint8_t*)malloc(poolSize);
    if (!pool) {
        poolSize = 8192;
        pool = (uint8_t*)malloc(poolSize);
    }
    if (!pool) {
        poolSize = 4096;
        pool = (uint8_t*)malloc(poolSize);
    }
    if (!pool) {
        f.close();
        return false;
    }

    bool isThumbnail = (maxW <= MAX_CACHE_W && maxH <= MAX_CACHE_H);
    uint8_t* cacheBuf = nullptr;
    CoverSlot* slot = nullptr;
    if (isThumbnail) {
        slot = takeSlot();
        cacheBuf = slot->bits;
    } else {
        // For sleep screen, allocate temporary 1-bit buffer to write BMP cache
        size_t sleepBufSize = ((maxW + 7) / 8) * maxH;
        if (sleepBufSize > 0 && sleepBufSize < 65536) {
            cacheBuf = (uint8_t*)calloc(1, sleepBufSize);
        }
    }

    JpegContext ctx;
    ctx.file = &f;
    ctx.boxX = x;
    ctx.boxY = y;
    ctx.maxW = maxW;
    ctx.maxH = maxH;
    ctx.cacheBuf = cacheBuf;
    ctx.cacheW = maxW;
    ctx.cacheH = maxH;

    JDEC jd;
    JRESULT res = jd_prepare(&jd, tjpgdInput, pool, poolSize, &ctx);
    if (res != JDR_OK) {
        Serial.printf("[JPEG] jd_prepare error: %d for file %s (size %u bytes)\n", res, filePath, (unsigned int)f.size());
        if (!isThumbnail && cacheBuf) free(cacheBuf);
        free(pool);
        f.close();
        return false;
    }

    // Calculate final scaled dimensions preserving aspect ratio
    float s = (fit == COVER_FIT_CROP)
        ? ((float)maxW / jd.width > (float)maxH / jd.height ? (float)maxW / jd.width : (float)maxH / jd.height)
        : ((float)maxW / jd.width < (float)maxH / jd.height ? (float)maxW / jd.width : (float)maxH / jd.height);
    int finalW = (int)round(jd.width * s);
    int finalH = (int)round(jd.height * s);
    if (finalW < 1) finalW = 1;
    if (finalH < 1) finalH = 1;

    // For sleep screen covers, preserve full high-frequency DCT sharpness (scale = 0) unless image is gigantic (> 2048px)
    BYTE scale = 0;
    if (isThumbnail) {
        while (scale < 3 && ((jd.width >> (scale + 1)) >= (UINT)finalW && (jd.height >> (scale + 1)) >= (UINT)finalH)) {
            scale++;
        }
    } else {
        while (scale < 3 && (jd.width >> scale) > 2048) {
            scale++;
        }
    }

    ctx.srcW = (int)(jd.width >> scale);
    ctx.srcH = (int)(jd.height >> scale);
    ctx.finalW = finalW;
    ctx.finalH = finalH;
    ctx.targetX = x + (maxW - finalW) / 2;
    ctx.targetY = y + (maxH - finalH) / 2;

    ctx.useFloyd = false;
    ctx.band = nullptr;
    ctx.colMap = nullptr;
    ctx.accSum = nullptr;
    ctx.accCnt = nullptr;
    ctx.lastGray = nullptr;
    ctx.errCur = nullptr;
    ctx.errNext = nullptr;
    ctx.bandTop = 0;
    ctx.bandFilled = 0;
    ctx.curDstY = 0;

    size_t bandSize = (size_t)ctx.srcW * 16;
    ctx.band = (uint8_t*)malloc(bandSize);
    ctx.colMap = (uint16_t*)malloc(ctx.srcW * sizeof(uint16_t));
    ctx.accSum = (uint32_t*)calloc(finalW, sizeof(uint32_t));
    ctx.accCnt = (uint16_t*)calloc(finalW, sizeof(uint16_t));
    ctx.lastGray = (uint8_t*)calloc(finalW, sizeof(uint8_t));
    ctx.errCur = (int16_t*)calloc(finalW + 2, sizeof(int16_t));
    ctx.errNext = (int16_t*)calloc(finalW + 2, sizeof(int16_t));

    if (ctx.band && ctx.colMap && ctx.accSum && ctx.accCnt && ctx.lastGray && ctx.errCur && ctx.errNext) {
        ctx.useFloyd = true;
        for (int sx = 0; sx < ctx.srcW; ++sx) {
            int dx = (int32_t)sx * finalW / ctx.srcW;
            if (dx >= finalW) dx = finalW - 1;
            ctx.colMap[sx] = (uint16_t)dx;
        }
    } else {
        if (ctx.band) { free(ctx.band); ctx.band = nullptr; }
        if (ctx.colMap) { free(ctx.colMap); ctx.colMap = nullptr; }
        if (ctx.accSum) { free(ctx.accSum); ctx.accSum = nullptr; }
        if (ctx.accCnt) { free(ctx.accCnt); ctx.accCnt = nullptr; }
        if (ctx.lastGray) { free(ctx.lastGray); ctx.lastGray = nullptr; }
        if (ctx.errCur) { free(ctx.errCur); ctx.errCur = nullptr; }
        if (ctx.errNext) { free(ctx.errNext); ctx.errNext = nullptr; }
    }

    res = jd_decomp(&jd, tjpgdOutput, scale);
    if (res == JDR_OK && ctx.useFloyd) {
        flushBandRows(&ctx, ctx.srcH);
        while (ctx.curDstY < ctx.finalH) {
            finalizeDstRow(&ctx);
            ctx.curDstY++;
        }
    }

    if (ctx.band) free(ctx.band);
    if (ctx.colMap) free(ctx.colMap);
    if (ctx.accSum) free(ctx.accSum);
    if (ctx.accCnt) free(ctx.accCnt);
    if (ctx.lastGray) free(ctx.lastGray);
    if (ctx.errCur) free(ctx.errCur);
    if (ctx.errNext) free(ctx.errNext);
    if (res != JDR_OK) {
        Serial.printf("[JPEG] jd_decomp error: %d\n", res);
    } else {
        if (isThumbnail) {
            commitSlot(slot, filePath, maxW, maxH, fit);
            Serial.printf("[JPEG] Cover decoded & cached in RAM (%dx%d, %lu ms).\n", maxW, maxH, millis() - t0);

            // Write persistent thumbnail BMP to SD card
            String pathStr(filePath);
            int slash = pathStr.lastIndexOf('/');
            String base = (slash >= 0) ? pathStr.substring(slash + 1) : pathStr;
            int dot = base.lastIndexOf('.');
            if (dot > 0) base = base.substring(0, dot);
            String bmpThumb = "/covers/" + base + ((fit == COVER_FIT_CROP) ? "_thumb_crop.bmp" : "_thumb_fit.bmp");
            save1BitBmp(bmpThumb.c_str(), maxW, maxH, slot->bits);
        } else if (cacheBuf) {
            // Write persistent sleep screen BMP to SD card
            String pathStr(filePath);
            int slash = pathStr.lastIndexOf('/');
            String base = (slash >= 0) ? pathStr.substring(slash + 1) : pathStr;
            int dot = base.lastIndexOf('.');
            if (dot > 0) base = base.substring(0, dot);
            String bmpSleep = "/covers/" + base + ((fit == COVER_FIT_CROP) ? "_crop.bmp" : "_fit.bmp");
            save1BitBmp(bmpSleep.c_str(), maxW, maxH, cacheBuf);
            Serial.printf("[JPEG] Sleep cover rendered and cached to %s (%lu ms).\n", bmpSleep.c_str(), millis() - t0);
        }
    }

    if (!isThumbnail && cacheBuf) free(cacheBuf);
    free(pool);
    f.close();
    return (res == JDR_OK);
}

void JpegViewer::drawCoverPlaceholder(const char* title, const char* author, int x, int y, int w, int h) {
    using namespace uikit;
    const Rect r{(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
    // 1. White book with a double border and an inner frame
    fill(r, Color::White);
    frameRect(r, Color::Black, 2);
    frameRect(insetXY(r, 6, 6), Color::Black);
    // 2. Spine on the left
    vline((int16_t)(x + 14), (int16_t)(y + 6), (int16_t)(y + h - 6), Color::Black, 1);
    vline((int16_t)(x + 16), (int16_t)(y + 6), (int16_t)(y + h - 6), Color::Black, 1);
    const int16_t tx = (int16_t)(x + 22), tw = (int16_t)(w - 22 - 12);
    // 3. Top band
    Rect band{tx, (int16_t)(y + 14), tw, (int16_t)(lineHeight(FontSmall) + 4)};
    fill(band, Color::Black);
    text(band, "BOOK ORBIT", FontSmall, Color::White, TextAlign::Center);
    // 4. Open book glyph
    const int16_t cx = (int16_t)(tx + tw / 2), cy = (int16_t)(y + h / 2 - 20);
    frameRect(Rect{(int16_t)(cx - 24), (int16_t)(cy - 16), 22, 32}, Color::Black);
    frameRect(Rect{(int16_t)(cx + 2), (int16_t)(cy - 16), 22, 32}, Color::Black);
    for (int dy = -10; dy <= 10; dy += 5) {
        hline((int16_t)(cx - 20), (int16_t)(cx - 6), (int16_t)(cy + dy));
        hline((int16_t)(cx + 6), (int16_t)(cx + 20), (int16_t)(cy + dy));
    }
    // 5. Title (up to 3 lines) and author
    const int16_t authorH = (author && *author) ? (int16_t)(lineHeight(FontSmall) + 14) : 0;
    if (title && *title) {
        Rect tr{tx, (int16_t)(cy + 24), tw, (int16_t)(y + h - authorH - 10 - (cy + 24))};
        text(tr, title, FontBody, Color::Black, TextAlign::Center, 3);
    }
    if (authorH) {
        hline(tx, (int16_t)(tx + tw), (int16_t)(y + h - authorH - 4));
        text(Rect{tx, (int16_t)(y + h - authorH), tw, (int16_t)(authorH - 8)}, author, FontSmall, Color::Black,
             TextAlign::Center);
    }
}
