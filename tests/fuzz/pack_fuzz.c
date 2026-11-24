/* A libFuzzer target for the level pack's parsers (src/client/pack.h): the manifest, the level file's outside
 * check, the script's check, a pack from bytes with its models.bin, and the reader of a ROM's level files. A
 * manifest that parses has a name of printable characters and a level in range.
 *
 * Build and run: make fuzz (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/bankranges.h"
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
    /* A pack from bytes, as one from a host: the input's first six bytes cut the rest into a manifest, a level
       file, a script and a models.bin. Bytes 0 and 1 give the manifest's length (10 bits) and the strict flag (the
       top bit), 2 and 3 the level file's, 4 and 5 the script's, each modulo what is left; models.bin is the rest.
       A pack that comes out has parts within the limits, and reads the same a second time. Its models passed the
       bank check without the ROM's index (bankcheck_fuzz.c runs the rules that need one). The buffer the game reads,
       filled by g64_pack_layout_fill, holds every part where the layout says. */
    if (size >= 6) {
        static const g64_pack_banks table = {g64_bank_exists, NULL, NULL};
        const uint8_t *rest = data + 6;
        size_t n = size - 6, mn = (size_t)(data[0] | (data[1] & 3) << 8) % (n + 1);
        size_t ln = (size_t)(data[2] | data[3] << 8) % (n - mn + 1);
        size_t sn = (size_t)(data[4] | data[5] << 8) % (n - mn - ln + 1), on = n - mn - ln - sn;
        const uint8_t *script = sn ? rest + mn + ln : NULL, *models = on ? rest + mn + ln + sn : NULL;
        char err[160];
        g64_pack a, b;
        if (g64_pack_from_bytes(rest, mn, rest + mn, ln, script, sn, models, on, &table, data[1] >> 7, &a, err,
                                sizeof err) == 0) {
            g64_pack_layout l;
            if (a.manifestSize != mn || a.levSize != ln || a.scriptSize != sn || a.modelsSize != on || a.level < 0 ||
                a.level >= G64_PACK_LEVELS || (data[1] >> 7 && g64_pack_check_strict(&a, NULL) != NULL))
                __builtin_trap();
            if (a.models && (a.modelsSize > G64_PACK_MODELS_MAX ||
                             g64_models_check(a.models, a.modelsSize, g64_bank_exists, NULL, NULL, NULL) != NULL ||
                             (uint64_t)a.modelsInfo.texAt + a.modelsInfo.texSize + a.modelsInfo.objSize !=
                                 a.modelsSize))
                __builtin_trap();
            g64_pack_layout_of(&a, 16, &l);
            if (l.size != (l.texSize ? l.objAt + l.objSize : 16 + ln + sn) || l.levAt != 16 || l.texAt % 8 ||
                (l.texSize && l.texAt < 16 + ln + sn))
                __builtin_trap();
            /* The buffer: the 16 bytes that stand for the ROM stay, each part is where the layout says, and the
               bytes between the script and the texture bank are 0. The buffer is exactly l.size bytes, so a write
               past it is caught by the address sanitizer. */
            {
                uint8_t *buf = (uint8_t *)malloc(l.size);
                size_t i, gapAt = l.scriptAt + sn, gapEnd = l.texSize ? l.texAt : l.size;
                if (!buf)
                    __builtin_trap();
                memset(buf, 0xA5, 16);
                g64_pack_layout_fill(&a, &l, buf);
                for (i = 0; i < 16; i++)
                    if (buf[i] != 0xA5)
                        __builtin_trap();
                if (memcmp(buf + l.levAt, a.lev, ln) != 0 || (sn && memcmp(buf + l.scriptAt, a.script, sn) != 0) ||
                    l.scriptAt != l.levAt + ln || gapAt > gapEnd)
                    __builtin_trap();
                for (i = gapAt; i < gapEnd; i++)
                    if (buf[i] != 0)
                        __builtin_trap();
                if (l.texSize && (l.texSize != a.modelsInfo.texSize || l.objSize != a.modelsInfo.objSize ||
                                  l.objAt != l.texAt + l.texSize ||
                                  memcmp(buf + l.texAt, a.models + a.modelsInfo.texAt, l.texSize + l.objSize) != 0))
                    __builtin_trap();
                free(buf);
            }
            if (g64_pack_from_bytes(a.manifest, a.manifestSize, a.lev, a.levSize, a.script, a.scriptSize, a.models,
                                    a.modelsSize, &table, 1, &b, err, sizeof err) == 0) {
                if (memcmp(a.id, b.id, sizeof a.id) != 0)
                    __builtin_trap();
                g64_pack_free(&b);
            }
            g64_pack_free(&a);
        } else if (a.lev || a.manifest || a.script || a.models) {
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
