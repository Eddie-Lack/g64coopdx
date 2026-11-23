/* A libFuzzer target for the level file's structure check (src/client/levcheck.h). The check must never read
 * outside the file, whatever the file holds: the input goes into a block of exactly its size, so the sanitizer
 * sees any read past it. Every token the check names starts inside the file.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/levcheck.h"

static size_t fileSize;

static void token(void *user, size_t offset, int t) {
    (void)user;
    (void)t;
    if (offset + 2 > fileSize)
        __builtin_trap();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t *file;
    size_t at = 0;
    if (size == 0)
        return 0;
    file = malloc(size);
    if (!file)
        return 0;
    memcpy(file, data, size);
    /* Most inputs get their own size as their first word, which the check asks for before it walks the tokens. */
    if (size >= 8 && (data[size - 1] & 3) != 0) {
        file[0] = (uint8_t)(size >> 24);
        file[1] = (uint8_t)(size >> 16);
        file[2] = (uint8_t)(size >> 8);
        file[3] = (uint8_t)size;
    }
    fileSize = size;
    g64_lev_check(file, size, token, NULL, &at);
    if (at > size)
        __builtin_trap();
    free(file);
    return 0;
}
