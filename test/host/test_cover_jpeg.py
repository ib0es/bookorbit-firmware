#!/usr/bin/env python3
"""Compare the firmware cover decoder with libjpeg on high-contrast edges."""

import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#define jd_prepare bookorbit_cover_jd_prepare
#define jd_decomp bookorbit_cover_jd_decomp
#include "freeink-sdk/libs/book/FreeInkBook/third_party/tjpgd/tjpgd.h"
static FILE* input;
static unsigned char output[64*64];
static size_t read_jpeg(JDEC* jd, unsigned char* buffer, size_t n) {
    (void)jd;
    if (buffer) return fread(buffer, 1, n, input);
    return fseek(input, (long)n, SEEK_CUR) == 0 ? n : 0;
}
static int on_pixels(JDEC* jd, void* data, JRECT* rect) {
    unsigned char* p = data;
    for (unsigned y=rect->top; y<=rect->bottom; ++y)
        for (unsigned x=rect->left; x<=rect->right; ++x)
            output[y*jd->width+x] = *p++;
    return 1;
}
int main(int argc, char** argv) {
    if(argc != 3) return 2;
    input = fopen(argv[1], "rb");
    if(!input) return 3;
    unsigned char pool[16384];
    JDEC jd;
    if(jd_prepare(&jd, read_jpeg, pool, sizeof(pool), NULL) != JDR_OK) return 4;
    if(jd.width != 64 || jd.height != 64) return 5;
    if(jd_decomp(&jd, on_pixels, 0) != JDR_OK) return 6;
    FILE* out = fopen(argv[2], "wb");
    if(!out) return 7;
    fwrite(output, 1, sizeof(output), out);
    fclose(out);
    fclose(input);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp_dir:
    tmp = Path(temp_dir)
    source = tmp / 'decode.c'
    source.write_text(HARNESS)
    exe = tmp / 'decode'
    subprocess.run(
        ['cc', '-std=c99', '-I', str(ROOT), str(source), str(ROOT / 'src/ui/vendor/cover_tjpgd.c'), '-o', str(exe)],
        check=True,
    )
    image = Image.new('L', (64, 64), 255)
    draw = ImageDraw.Draw(image)
    draw.rectangle((8, 6, 30, 50), fill=0)
    draw.line((0, 63, 63, 0), fill=0, width=3)
    draw.text((32, 24), 'AB', fill=0)
    for quality in (50, 75, 95):
        jpeg, raw = tmp / 'edges.jpg', tmp / 'decoded.raw'
        image.convert('RGB').save(jpeg, quality=quality)
        expected = Image.open(jpeg).convert('L').tobytes()
        subprocess.run([str(exe), str(jpeg), str(raw)], check=True)
        errors = [abs(a - b) for a, b in zip(expected, raw.read_bytes())]
        assert len(errors) == 4096
        assert max(errors) <= 4, f'JPEG quality {quality}: error {max(errors)} (sample wrapping?)'
print('Cover JPEG clipping: OK')
