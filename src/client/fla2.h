/* FLA2, the compression of the game's banks: an LZ format with a 4096-byte window.
 *
 * A stream starts with "FLA2" and the size of its data, as a 32-bit little-endian word. Then come groups of a flag
 * byte and 8 items, high bit first. A clear bit is one literal byte. A set bit is two bytes b0 and b1: b0 of 0 ends
 * the stream; otherwise the item copies (b0 & 15) + 2 bytes from (b0 >> 4) * 256 + b1 bytes back in the window. The
 * window starts as zeros. This is the game's decompressor (weemitt/src/game/decompress.c), which ends a stream on
 * b0 alone.
 *
 * The game's decompressor has no bounds. This one reads and writes only inside its buffers, and refuses a stream
 * that ends early, runs past its source, or gives more or fewer bytes than its header says. Every byte of the
 * source is untrusted. It is ported from Glovebox's fla2.rs (the owner's code), which was ported from libgarib
 * (MIT, THIRD_PARTY_NOTICES.md). */
#ifndef G64_FLA2_H
#define G64_FLA2_H

#include <stddef.h>
#include <stdint.h>

/* Whether the source starts with "FLA2" and has its whole header. */
int g64_fla2_is(const uint8_t *src, size_t n);

/* The size of the stream's data, from its header. Returns NULL and the size, or a message when the source is not
 * an FLA2 stream or the size is more than max. */
const char *g64_fla2_size(const uint8_t *src, size_t n, size_t max, size_t *size);

/* Decompresses the stream into dst, which holds dstSize bytes. dstSize must be the size from g64_fla2_size.
 * Returns NULL when the stream gives exactly dstSize bytes, or a message. On an error the content of dst is not
 * defined, but nothing outside it was written. */
const char *g64_fla2_decompress(const uint8_t *src, size_t n, uint8_t *dst, size_t dstSize);

#endif
