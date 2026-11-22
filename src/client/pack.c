/* A level pack (pack.h). */
#include "pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/monocypher/monocypher.h"

/* One "key=value" line of the manifest, without its end. Returns 0, or -1. seen has a bit for each key so far. */
static int manifest_line(const char *line, size_t len, g64_pack *out, unsigned *seen) {
    size_t i;
    if (len > 7 && memcmp(line, "format=", 7) == 0) {
        if ((*seen & 1) || len != 8 || line[7] != '1')
            return -1;
        *seen |= 1;
        return 0;
    }
    if (len > 5 && memcmp(line, "name=", 5) == 0) {
        if ((*seen & 2) || len - 5 > G64_PACK_NAME)
            return -1;
        for (i = 5; i < len; i++)
            if (line[i] < 32 || line[i] > 126)
                return -1;
        memcpy(out->name, line + 5, len - 5);
        out->name[len - 5] = 0;
        *seen |= 2;
        return 0;
    }
    if (len > 6 && memcmp(line, "level=", 6) == 0) {
        int v = 0;
        if ((*seen & 4) || len - 6 > 2)
            return -1;
        for (i = 6; i < len; i++) {
            if (line[i] < '0' || line[i] > '9')
                return -1;
            v = v * 10 + (line[i] - '0');
        }
        /* No leading zero: one way to write each number. */
        if (v >= G64_PACK_LEVELS || (len - 6 == 2 && line[6] == '0'))
            return -1;
        out->level = v;
        *seen |= 4;
        return 0;
    }
    return -1;
}

int g64_pack_parse_manifest(const uint8_t *d, size_t n, g64_pack *out) {
    g64_pack p;
    unsigned seen = 0;
    size_t at = 0;
    if (!d || !out || n == 0 || n > G64_PACK_MANIFEST_MAX)
        return -1;
    memset(&p, 0, sizeof p);
    while (at < n) {
        size_t end = at, len;
        while (end < n && d[end] != '\n')
            end++;
        /* Every line ends with a line feed alone. A carriage return is refused like any other control character:
           the pack's id is a hash of the file's bytes, so a manifest has one way to be written. */
        if (end == n)
            return -1;
        len = end - at;
        if (manifest_line((const char *)d + at, len, &p, &seen) != 0)
            return -1;
        at = end + 1;
    }
    if (seen != 7)
        return -1;
    memcpy(out->name, p.name, sizeof out->name);
    out->level = p.level;
    return 0;
}

int g64_pack_check_level(const uint8_t *lev, size_t n) {
    uint32_t size;
    if (!lev || n < 8 || n > G64_PACK_LEVEL_MAX)
        return -1;
    size = (uint32_t)lev[0] << 24 | (uint32_t)lev[1] << 16 | (uint32_t)lev[2] << 8 | lev[3];
    return size == n ? 0 : -1;
}

static void hash_file(crypto_blake2b_ctx *ctx, const char *name, const uint8_t *d, size_t n) {
    uint8_t len[4] = {(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)(n >> 16), (uint8_t)(n >> 24)};
    crypto_blake2b_update(ctx, (const uint8_t *)name, strlen(name) + 1);
    crypto_blake2b_update(ctx, len, sizeof len);
    crypto_blake2b_update(ctx, d, n);
}

static uint32_t rom_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* The ROM offset that two instructions of the game's code load: "lui REG, high" and then "addiu REG, REG, low" or
 * "ori REG, REG, low". The game's loader has such a pair for the start of the level files and one for their end,
 * and Glovebox changes them when it moves the files. Returns 0 when the words at that place are not such a pair, or
 * the address is not in the cartridge. */
static uint32_t code_pointer(const uint8_t *rom, size_t size, size_t at) {
    uint32_t hi, lo, addr, op;
    if (size < 8 || at > size - 8)
        return 0;
    hi = rom_be32(rom + at);
    lo = rom_be32(rom + at + 4);
    /* The second instruction is "addiu" in the game's own ROM, which adds a signed low half, and "ori" in a ROM
     * that Glovebox built, which puts in an unsigned one. */
    op = lo >> 26;
    if ((hi >> 26) != 0x0F || (op != 0x09 && op != 0x0D) || ((hi >> 16) & 31) != ((lo >> 16) & 31) ||
        ((lo >> 21) & 31) != ((lo >> 16) & 31))
        return 0;
    addr = ((hi & 0xFFFF) << 16) + (lo & 0xFFFF) - ((op == 0x09 && (lo & 0x8000)) ? 0x10000u : 0u);
    /* The cartridge is at 0xB0000000 in the game's addresses. */
    if ((addr >> 28) != 0xB)
        return 0;
    return addr & 0x0FFFFFFF;
}

/* Where the game's code keeps the two pairs (weemitt's LoadLandscapeFile; the ROM's map in libgarib's formats). */
#define LEVELS_START_CODE 341164
#define LEVELS_END_CODE 341348

const char *g64_pack_level_from_rom(const uint8_t *rom, size_t size, int num, size_t *at, size_t *levSize) {
    uint32_t pos, end, n;
    int i;
    if (size < 0x1000 || rom_be32(rom) != 0x80371240u)
        return "this is not a ROM in the .z64 byte order";
    if (num < 0 || num >= G64_PACK_LEVELS)
        return "the game has no level with that number";
    pos = code_pointer(rom, size, LEVELS_START_CODE);
    end = code_pointer(rom, size, LEVELS_END_CODE);
    if (pos == 0 || end == 0 || pos >= end || end > size)
        return "the ROM's code does not say where the level files are";
    for (i = 0;; i++) {
        if (end - pos < 8)
            return "the ROM has no level file with that number";
        n = rom_be32(rom + pos);
        if (n < 8 || n > end - pos)
            return "a level file in the ROM has a wrong size";
        if (i == num)
            break;
        pos += n;
    }
    if (n > G64_PACK_LEVEL_MAX)
        return "the level file is too large for a pack";
    *at = pos;
    *levSize = n;
    return NULL;
}

int g64_pack_check_script(const uint8_t *script, size_t n) {
    if (!script || n < 1 || n > G64_PACK_SCRIPT_MAX || script[0] == 0x1B)
        return -1;
    return memchr(script, 0, n) ? -1 : 0;
}

void g64_pack_make_id(const uint8_t *manifest, size_t manifestSize, const uint8_t *lev, size_t levSize,
                      const uint8_t *script, size_t scriptSize, uint8_t id[G64_PACK_ID]) {
    static const char head[] = "g64pack 1";
    uint8_t hash[32];
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, sizeof hash);
    crypto_blake2b_update(&ctx, (const uint8_t *)head, sizeof head);
    hash_file(&ctx, "pack.txt", manifest, manifestSize);
    hash_file(&ctx, "level.lev", lev, levSize);
    if (script)
        hash_file(&ctx, "script.lua", script, scriptSize);
    crypto_blake2b_final(&ctx, hash);
    memcpy(id, hash, G64_PACK_ID);
}

/* Reads a whole file of at most max bytes. Returns its bytes (the caller frees them), or NULL. */
static uint8_t *read_file(const char *dir, const char *name, size_t max, size_t *size) {
    char path[1024];
    uint8_t *d;
    size_t n;
    FILE *f;
    if (snprintf(path, sizeof path, "%s/%s", dir, name) >= (int)sizeof path)
        return NULL;
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    d = malloc(max + 1);
    n = d ? fread(d, 1, max + 1, f) : 0;
    fclose(f);
    if (!d || n == 0 || n > max) {
        free(d);
        return NULL;
    }
    *size = n;
    return d;
}

int g64_pack_load(const char *dir, g64_pack *out, char *err, size_t errSize) {
    size_t manifestSize = 0, levSize = 0;
    uint8_t *manifest, *lev;
    memset(out, 0, sizeof *out);
    out->level = -1;
    manifest = read_file(dir, "pack.txt", G64_PACK_MANIFEST_MAX, &manifestSize);
    if (!manifest) {
        snprintf(err, errSize, "the level pack has no pack.txt, or it is too long");
        return -1;
    }
    if (g64_pack_parse_manifest(manifest, manifestSize, out) != 0) {
        free(manifest);
        snprintf(err, errSize, "the level pack's pack.txt is not as it must be (format=1, name=, level=)");
        return -1;
    }
    lev = read_file(dir, "level.lev", G64_PACK_LEVEL_MAX, &levSize);
    if (!lev || g64_pack_check_level(lev, levSize) != 0) {
        free(manifest);
        free(lev);
        snprintf(err, errSize, "the level pack has no level.lev, or it is not a level file");
        return -1;
    }
    {
        /* The script is optional: a file that is there must be a script. */
        char path[1024];
        FILE *f = NULL;
        size_t scriptSize = 0;
        uint8_t *script = NULL;
        if (snprintf(path, sizeof path, "%s/script.lua", dir) < (int)sizeof path)
            f = fopen(path, "rb");
        if (f) {
            fclose(f);
            script = read_file(dir, "script.lua", G64_PACK_SCRIPT_MAX, &scriptSize);
            if (!script || g64_pack_check_script(script, scriptSize) != 0) {
                free(manifest);
                free(lev);
                free(script);
                snprintf(err, errSize, "the level pack's script.lua is empty, too long, or not Lua source text");
                return -1;
            }
        }
        g64_pack_make_id(manifest, manifestSize, lev, levSize, script, scriptSize, out->id);
        out->script = script;
        out->scriptSize = scriptSize;
    }
    free(manifest);
    out->lev = lev;
    out->levSize = levSize;
    return 0;
}

void g64_pack_free(g64_pack *p) {
    if (p) {
        free(p->lev);
        free(p->script);
        memset(p, 0, sizeof *p);
        p->level = -1;
    }
}
