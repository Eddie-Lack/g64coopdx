/* A libFuzzer target for the level pack's parsers (src/client/pack.h): the manifest and the level file's outside
 * check. A manifest that parses has a name of printable characters and a level in range.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../src/client/pack.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    g64_pack p;
    memset(&p, 0, sizeof p);
    if (g64_pack_parse_manifest(data, size, &p) == 0) {
        size_t i, n = strlen(p.name);
        if (n < 1 || n > G64_PACK_NAME || p.level < 0 || p.level >= G64_PACK_LEVELS)
            __builtin_trap();
        for (i = 0; i < n; i++)
            if (p.name[i] < 32 || p.name[i] > 126)
                __builtin_trap();
    }
    if (g64_pack_check_level(data, size) == 0 && (size < 8 || size > G64_PACK_LEVEL_MAX))
        __builtin_trap();
    if (g64_pack_check_script(data, size) == 0 && (size < 1 || size > G64_PACK_SCRIPT_MAX || data[0] == 0x1B ||
                                                    memchr(data, 0, size)))
        __builtin_trap();
    return 0;
}
