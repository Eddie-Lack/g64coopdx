/* A libFuzzer target for the level pack's parsers (src/client/pack.h): the manifest, the level file's outside
 * check, the script's check and the reader of a ROM's level files. A manifest that parses has a name of printable characters and a level in range.
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
    /* A pack from bytes, as one from a host: the input's first three bytes cut the rest into a manifest, a level
       file and a script. A pack that comes out has parts within the limits, and reads the same a second time. */
    if (size >= 3) {
        const uint8_t *rest = data + 3;
        size_t n = size - 3, mn = n ? (size_t)(data[0] | (data[1] & 3) << 8) % (n + 1) : 0;
        size_t ln = n - mn ? (size_t)data[2] * 37 % (n - mn + 1) : 0, sn = n - mn - ln;
        char err[160];
        g64_pack a, b;
        if (g64_pack_from_bytes(rest, mn, rest + mn, ln, sn ? rest + mn + ln : NULL, sn, data[1] >> 7, &a, err,
                                sizeof err) == 0) {
            if (a.manifestSize != mn || a.levSize != ln || a.scriptSize != sn || a.level < 0 ||
                a.level >= G64_PACK_LEVELS || (data[1] >> 7 && g64_pack_check_strict(&a, NULL) != NULL))
                __builtin_trap();
            if (g64_pack_from_bytes(a.manifest, a.manifestSize, a.lev, a.levSize, a.script, a.scriptSize, 1, &b, err,
                                    sizeof err) == 0) {
                if (memcmp(a.id, b.id, sizeof a.id) != 0)
                    __builtin_trap();
                g64_pack_free(&b);
            }
            g64_pack_free(&a);
        } else if (a.lev || a.manifest || a.script) {
            __builtin_trap();
        }
    }
    /* The reader of a ROM's level files: the input stands for a ROM, with its first word and its two code pointers
       put right so the walk over the files is reached. A file it finds lies inside the input. */
    if (size >= 4) {
        static uint8_t rom[0x60000];
        size_t at = 0, n = 0, len = size < sizeof rom - 0x54000 ? size : sizeof rom - 0x54000;
        int num = data[0] % 50;
        memset(rom, 0, sizeof rom);
        memcpy(rom + 0x54000, data, len);
        rom[0] = 0x80, rom[1] = 0x37, rom[2] = 0x12, rom[3] = 0x40;
        /* lui v0, 0xB005; ori v0, v0, 0x4000 and lui v1, 0xB005; ori v1, v1, 0x4000 + len, or the input's own words. */
        if (data[1] & 1) {
            static const uint8_t start[8] = {0x3C, 0x02, 0xB0, 0x05, 0x34, 0x42, 0x40, 0x00};
            uint8_t end[8] = {0x3C, 0x03, 0xB0, 0x05, 0x34, 0x63, 0, 0};
            end[6] = (uint8_t)((0x4000 + len) >> 8);
            end[7] = (uint8_t)(0x4000 + len);
            memcpy(rom + 341164, start, 8);
            memcpy(rom + 341348, end, 8);
        } else {
            memcpy(rom + 341164, data, len < 8 ? len : 8);
            memcpy(rom + 341348, data + (len > 8 ? 8 : 0), len < 16 ? (len > 8 ? len - 8 : 0) : 8);
        }
        if (g64_pack_level_from_rom(rom, 0x54000 + len, num, &at, &n) == NULL &&
            (n < 8 || n > G64_PACK_LEVEL_MAX || at > 0x54000 + len || n > 0x54000 + len - at))
            __builtin_trap();
    }
    return 0;
}
