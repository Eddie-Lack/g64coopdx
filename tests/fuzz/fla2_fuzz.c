/* A libFuzzer target for the bounded FLA2 reader (src/client/fla2.h) and the index of the ROM's banks
 * (src/client/rombanks.h). Neither may read or write outside its buffers, whatever the input holds. Every buffer
 * is a block of exactly its size, so the sanitizer sees any access past it.
 *
 *   - The reader decompresses the input into a block of the size its header gives, and into a block one byte
 *     smaller, which it must refuse.
 *   - The index walks the input as a ROM of one texture bank and one object bank, each the whole input. An input
 *     that starts with "FLA2" is decompressed first, as the game's loader does.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/fla2.h"
#include "../../src/client/rombanks.h"

/* A size past this is refused, which keeps each run fast. */
#define MAX_OUT (1u << 20)

static size_t romSize;

static int whole(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    (void)user;
    (void)kind;
    if (num != 0)
        return 0;
    *start = 0;
    *end = (uint32_t)romSize;
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t *src, *out;
    size_t n = 0;
    G64RomBanks *b;

    if (size == 0 || size > (1u << 20))
        return 0;
    src = malloc(size);
    if (!src)
        return 0;
    memcpy(src, data, size);
    /* Most inputs get the FLA2 header, with a size that the reader allows. */
    if (size >= 8 && (data[size - 1] & 3) != 0) {
        memcpy(src, "FLA2", 4);
        src[6] &= 0x0F;
        src[7] = 0;
    }
    if (g64_fla2_size(src, size, MAX_OUT, &n) == NULL) {
        out = malloc(n ? n : 1);
        if (out) {
            g64_fla2_decompress(src, size, out, n);
            free(out);
        }
        if (n > 0) {
            out = malloc(n - 1 ? n - 1 : 1);
            if (out) {
                if (g64_fla2_decompress(src, size, out, n - 1) == NULL)
                    __builtin_trap();
                free(out);
            }
        }
        /* The index allows larger banks. A size it would allocate stays below MAX_OUT here. */
        romSize = size;
        b = g64_rombanks_build(src, size, whole, NULL);
        g64_rombanks_free(b);
    } else if (!g64_fla2_is(src, size)) {
        romSize = size;
        b = g64_rombanks_build(src, size, whole, NULL);
        if (b) {
            uint8_t h[16];
            memset(h, 0, sizeof h);
            g64_rombanks_has_object_id(b, 0);
            g64_rombanks_has_texture_id(b, 0);
            g64_rombanks_has_texels_hash(b, h);
            g64_rombanks_has_verts_hash(b, h);
            g64_rombanks_has_vtx_hash(b, h);
        }
        g64_rombanks_free(b);
    }
    free(src);
    return 0;
}
