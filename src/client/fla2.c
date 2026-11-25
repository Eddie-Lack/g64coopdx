/* The bounded FLA2 reader (fla2.h). Ported from Glovebox's crates/core/src/fla2.rs (the owner's code), with the
 * end of a stream as the game reads it. Glovebox ported it from libgarib (MIT, THIRD_PARTY_NOTICES.md). */
#include "fla2.h"

#include <string.h>

int g64_fla2_is(const uint8_t *src, size_t n) {
    return src != NULL && n >= 8 && memcmp(src, "FLA2", 4) == 0;
}

const char *g64_fla2_size(const uint8_t *src, size_t n, size_t max, size_t *size) {
    uint32_t length;

    if (!g64_fla2_is(src, n))
        return "not an FLA2 stream";
    length = (uint32_t)src[4] | (uint32_t)src[5] << 8 | (uint32_t)src[6] << 16 | (uint32_t)src[7] << 24;
    if (length > max)
        return "the FLA2 stream is larger than allowed";
    *size = length;
    return NULL;
}

const char *g64_fla2_decompress(const uint8_t *src, size_t n, uint8_t *dst, size_t dstSize) {
    uint8_t window[0x1000];
    size_t cursor = 0, i = 8, out = 0, length;
    const char *why = g64_fla2_size(src, n, (size_t)0xFFFFFFFFu, &length);

    if (why)
        return why;
    if (length != dstSize)
        return "the FLA2 stream's size isn't the buffer's";
    memset(window, 0, sizeof window);
    for (;;) {
        unsigned cmd;
        int bit;

        if (i >= n)
            return "the FLA2 stream ends early";
        cmd = src[i++];
        for (bit = 7; bit >= 0; bit--) {
            if (((cmd >> bit) & 1) == 0) {
                uint8_t b;
                if (i >= n)
                    return "the FLA2 stream ends early";
                if (out >= dstSize)
                    return "the FLA2 stream is longer than it says";
                b = src[i++];
                dst[out++] = b;
                window[cursor] = b;
                cursor = (cursor + 1) & 0xFFF;
            } else {
                unsigned b0, b1, len, dist, k;
                size_t rd;
                if (i >= n)
                    return "the FLA2 stream ends early";
                b0 = src[i++];
                /* The game ends the stream on a zero first byte, without reading a second one. */
                if (b0 == 0) {
                    if (out != dstSize)
                        return "the FLA2 stream ends before its size";
                    return NULL;
                }
                if (i >= n)
                    return "the FLA2 stream ends early";
                b1 = src[i++];
                len = (b0 & 0x0F) + 2;
                dist = ((b0 & 0xF0) << 4) + b1;
                if (len > dstSize - out)
                    return "the FLA2 stream is longer than it says";
                rd = (cursor - dist) & 0xFFF;
                for (k = 0; k < len; k++) {
                    uint8_t b = window[rd];
                    rd = (rd + 1) & 0xFFF;
                    window[cursor] = b;
                    cursor = (cursor + 1) & 0xFFF;
                    dst[out++] = b;
                }
            }
        }
    }
}
