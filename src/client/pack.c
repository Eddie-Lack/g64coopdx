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

void g64_pack_make_id(const uint8_t *manifest, size_t manifestSize, const uint8_t *lev, size_t levSize,
                      uint8_t id[G64_PACK_ID]) {
    static const char head[] = "g64pack 1";
    uint8_t hash[32];
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, sizeof hash);
    crypto_blake2b_update(&ctx, (const uint8_t *)head, sizeof head);
    hash_file(&ctx, "pack.txt", manifest, manifestSize);
    hash_file(&ctx, "level.lev", lev, levSize);
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
    g64_pack_make_id(manifest, manifestSize, lev, levSize, out->id);
    free(manifest);
    out->lev = lev;
    out->levSize = levSize;
    return 0;
}

void g64_pack_free(g64_pack *p) {
    if (p) {
        free(p->lev);
        memset(p, 0, sizeof *p);
        p->level = -1;
    }
}
