#!/usr/bin/env python3
"""Exercise the production streaming JPEG resampler without device hardware."""

import platform
import subprocess
import tempfile
from pathlib import Path

source = (Path(__file__).resolve().parents[2] / 'src/ui/jpeg_viewer.cpp').read_text()
context = source[source.index('struct JpegContext {') : source.index('// Fast 1-bit RAM cache')]
functions = source[source.index('static bool coverDitherBlack') : source.index('// Output function for TJpgDec')]
harness = r'''
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cassert>
struct FsFile {};
enum EPDColor { EPD_WHITE, EPD_BLACK };
struct {
    int width;
    std::vector<EPDColor> pixels;
    void drawPixel(int x, int y, EPDColor color) { pixels.at(y * width + x) = color; }
} Display;
'''
main = r'''
int main() {
    // Flat tones retain their ink coverage, extremes stay clean, and repeated
    // renders are deterministic (important when reopening a persistent cache).
    for (int gray : {0, 16, 64, 128, 192, 240, 255}) {
        const int w = 257, h = 193;
        std::vector<EPDColor> first;
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<int16_t> cur(w+2), next(w+2);
            std::vector<EPDColor> pixels;
            int blackCount = 0;
            for (int y = 0; y < h; ++y) {
                const int dir = (y & 1) ? -1 : 1;
                std::vector<EPDColor> row(w);
                for (int x = dir > 0 ? 0 : w-1; x >= 0 && x < w; x += dir) {
                    bool black = coverDitherBlack(gray, x, y, dir, cur.data(), next.data());
                    row[x] = black ? EPD_BLACK : EPD_WHITE;
                    blackCount += black;
                }
                pixels.insert(pixels.end(), row.begin(), row.end());
                cur.swap(next);
                std::fill(next.begin(), next.end(), 0);
            }
            const double expected = (255-gray)/255.0;
            const double actual = blackCount / double(w*h);
            assert(actual >= expected-0.015 && actual <= expected+0.015);
            if(gray == 0) assert(blackCount == w*h);
            if(gray == 255) assert(blackCount == 0);
            if(gray == 128) {
                // A mid-gray must not collapse into an almost perfect
                // checkerboard, which shows as mesh/moire on cover backgrounds.
                int alternating = 0;
                for(int y=0;y<h;++y) for(int x=1;x<w;++x)
                    alternating += pixels[y*w+x] != pixels[y*w+x-1];
                assert(alternating < 0.85 * (w-1) * h);
            }
            if(pass == 0) first = pixels;
            else assert(first == pixels);
        }
    }

    // Noninteger enlargement, reduction, mixed axes, and identity. A white
    // source must remain white everywhere; stripes must follow inverse mapping.
    for (auto dims : {std::vector<int>{7,9,13,17}, {13,17,7,9},
                      {7,17,13,9}, {13,9,7,17}, {7,9,7,9}}) {
        int sw=dims[0], sh=dims[1], dw=dims[2], dh=dims[3];
        for (bool stripes : {false, true}) {
            JpegContext c{};
            c.srcW=sw; c.srcH=sh; c.finalW=dw; c.finalH=dh;
            c.maxW=dw; c.maxH=dh;
            std::vector<uint8_t> band(sw*16), last(dw);
            std::vector<uint16_t> columns(sw), counts(dw);
            std::vector<uint32_t> sums(dw);
            std::vector<int16_t> current(dw+2), next(dw+2);
            c.band=band.data(); c.colMap=columns.data(); c.accCnt=counts.data();
            c.accSum=sums.data(); c.lastGray=last.data();
            c.errCur=current.data(); c.errNext=next.data();
            for(int x=0;x<sw;++x) columns[x]=x*dw/sw;
            Display.width=dw; Display.pixels.assign(dw*dh, EPD_BLACK);
            for(int y=0;y<sh;++y) {
                for(int x=0;x<sw;++x)
                    band[(y%16)*sw+x] = stripes && (y%2) ? 0 : 255;
                flushBandRows(&c, y+1);
            }
            while(c.curDstY<dh) { finalizeDstRow(&c); ++c.curDstY; }
            if(!stripes) for(auto p: Display.pixels) assert(p==EPD_WHITE);
            if(stripes && dh>=sh) for(int y=0;y<dh;++y)
                for(int x=0;x<dw;++x)
                    assert(Display.pixels[y*dw+x] == ((y*sh/dh)%2 ? EPD_BLACK : EPD_WHITE));
        }
    }
}
'''
with tempfile.TemporaryDirectory() as temp:
    cpp = Path(temp) / 'resampling.cpp'
    cpp.write_text(harness + context + functions + main)
    binary = Path(temp) / 'resampling'
    flags = (
        ['-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1']
        if platform.system() == 'Darwin'
        else []
    )
    subprocess.run(['c++', '-std=c++17', *flags, str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Cover resampling: OK')
