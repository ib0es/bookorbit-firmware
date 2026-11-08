#pragma once
// Linux stand-in for macOS CommonCrypto (used by simulator/include/MD5Builder.h):
// a small real MD5 (RFC 1321), so KOReader document ids and the KOSync
// x-auth-key are the same as on the device.
#include <cstdint>
#include <cstring>
typedef uint32_t CC_LONG;
#define CC_MD5_DIGEST_LENGTH 16
typedef struct { uint32_t h[4]; uint64_t len; uint8_t buf[64]; size_t n; } CC_MD5_CTX;

namespace cc_md5_detail {
inline uint32_t rol(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
inline void block(uint32_t h[4], const uint8_t* p) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int R[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                              4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t w[16];
    for (int i = 0; i < 16; i++) w[i] = p[i*4] | p[i*4+1] << 8 | p[i*4+2] << 16 | (uint32_t)p[i*4+3] << 24;
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
        else { f = c ^ (b | ~d); g = (7 * i) % 16; }
        uint32_t t = d; d = c; c = b; b = b + rol(a + f + K[i] + w[g], R[i]); a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
}
}  // namespace cc_md5_detail

inline int CC_MD5_Init(CC_MD5_CTX* c) {
    c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476;
    c->len = 0; c->n = 0; return 1;
}
inline int CC_MD5_Update(CC_MD5_CTX* c, const void* data, CC_LONG len) {
    const uint8_t* p = (const uint8_t*)data;
    c->len += len;
    while (len--) { c->buf[c->n++] = *p++; if (c->n == 64) { cc_md5_detail::block(c->h, c->buf); c->n = 0; } }
    return 1;
}
inline int CC_MD5_Final(unsigned char* md, CC_MD5_CTX* c) {
    uint64_t bits = c->len * 8;
    uint8_t pad = 0x80; CC_MD5_Update(c, &pad, 1);
    uint8_t z = 0; while (c->n != 56) CC_MD5_Update(c, &z, 1);
    for (int i = 0; i < 8; i++) { uint8_t b = (uint8_t)(bits >> (8 * i)); CC_MD5_Update(c, &b, 1); }
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) md[i*4+j] = (uint8_t)(c->h[i] >> (8 * j));
    return 1;
}
