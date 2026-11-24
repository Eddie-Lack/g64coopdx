/* The index of the ROM's banks (src/client/rombanks.c) and the bounded FLA2 reader (src/client/fla2.c) against the
 * game, with the ROM.
 *
 *   A. For every bank of the ROM that is an FLA2 stream, the reader gives the same bytes as the game's own
 *      decompressor (DecompressBuffer).
 *   B. For every texture bank and object bank, the game loads the bank alone (LoadTextureBank or LoadObjectBank).
 *      An index of that bank alone must list exactly the ids the loaded bank has, and FindTexture or FindObject
 *      must find each of them. The loaded bank's pointers name its texels, vertex arrays and G_VTX blocks. The
 *      index of the bank must have the hash of the ROM's bytes at each of them.
 *   C. The index of the whole ROM skips no bank, and its queries find every id of B. It finds the hashes of one
 *      texture's texels, one vertex array and one G_VTX block of the ROM, and not those of a copy with one byte
 *      changed. It prints its counts. The time it took goes to stderr.
 *   D. The table of the game's bank switch (src/client/bankranges.h), for programs that do not link the game, has
 *      the same bank numbers and gives the same ranges on this ROM as the game's own switch (G64BankRom).
 *
 * The output holds counts only, no ids or hashes. make rombanks runs it on Linux and under Wine and compares the
 * outputs.
 *
 * Usage: rombanks ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../src/client/bankranges.h"
#include "../../src/client/fla2.h"
#include "../../src/client/rombanks.h"
#include "../../weemitt/bruteforce/engine/engine.h"

/* The game's bank slots (include/banks.h). Their pointers are 32-bit words in port_ram. */
extern struct {
    uint32_t textures, data;
    int32_t count;
} textureBanks[5];
extern struct {
    int32_t unk00;
    uint32_t data;
    int32_t count;
    uint32_t entries;
} objectBanks[5];
extern int32_t numCyclingTextures;
extern uintptr_t port_ptr_delta;
int G64BankRom(int kind, unsigned num, unsigned *start, unsigned *end);
void LoadTextureBank(unsigned num);
void LoadObjectBank(unsigned num);
void FindTexture(uint32_t *texPtr, uint32_t id, int verbose);
void FindObject(uint32_t *objPtr, uint32_t id, char *name, int vital);
void *JallocAlloc(int32_t size, int32_t clear, char *name);
int32_t DecompressBuffer(uint8_t *src, uint8_t *dst);

static int failed;

static const uint8_t *word(uint32_t w) {
    return (const uint8_t *)(uintptr_t)w + port_ptr_delta;
}

static int range_of(int kind, unsigned num, uint32_t *start, uint32_t *end) {
    unsigned s, e;
    if (!G64BankRom(kind, num, &s, &e))
        return 0;
    *start = s & 0x0FFFFFFF;
    *end = e & 0x0FFFFFFF;
    return 1;
}

/* The callback for an index of one bank. */
typedef struct {
    int kind;
    unsigned num;
} One;

static int one_range(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    const One *o = user;
    return kind == o->kind && num == o->num && range_of(kind, num, start, end);
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* Sorts ids and drops repeats; returns the new count. */
static size_t unique(uint32_t *ids, size_t n) {
    size_t i, k = 0;
    if (n == 0)
        return 0;
    qsort(ids, n, sizeof *ids, cmp_u32);
    for (i = 1; i < n; i++)
        if (ids[i] != ids[k])
            ids[++k] = ids[i];
    return k + 1;
}

/* The bytes of a bank, decompressed when they are an FLA2 stream. Returns NULL when the bank cannot be read. The
 * caller frees *buf. */
static const uint8_t *bank_bytes(const uint8_t *rom, size_t romSize, int kind, unsigned num, size_t *n, uint8_t **buf) {
    uint32_t start, end;
    size_t size;

    *buf = NULL;
    if (!range_of(kind, num, &start, &end) || end > romSize || start >= end)
        return NULL;
    if (!g64_fla2_is(rom + start, end - start)) {
        *n = end - start;
        return rom + start;
    }
    if (g64_fla2_size(rom + start, end - start, 1u << 24, &size) || (*buf = malloc(size ? size : 1)) == NULL)
        return NULL;
    if (g64_fla2_decompress(rom + start, end - start, *buf, size)) {
        free(*buf);
        *buf = NULL;
        return NULL;
    }
    *n = size;
    return *buf;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint16_t rd16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

/* The counts of hashes that B checked, over all banks. */
static unsigned loadedTexels, loadedVerts, loadedVtx;

/* Whether the index has the hash of n bytes at off in the bank's ROM bytes d. */
static int has_at(const G64RomBanks *b, int (*has)(const G64RomBanks *, const uint8_t *), const uint8_t *d,
                  size_t size, uint32_t off, size_t n) {
    uint8_t h[16];
    if (off > size || n > size - off)
        return 0;
    g64_rombanks_hash(h, d + off, n);
    return has(b, h);
}

/* B for the hashes: the game's loaded bank names texels, vertex arrays and G_VTX blocks by its relocated pointers.
 * A pointer less the bank's base is the offset in the bank. The index of the bank must have the hash of the ROM's
 * bytes at each of them. Returns 0 when one is missing. */
static int check_loaded_hashes(const G64RomBanks *b, int kind, const uint8_t *d, size_t n) {
    int32_t k;

    if (kind == G64_BANK_TEXTURE) {
        uint32_t base = textureBanks[0].data, at = textureBanks[0].textures;
        for (k = 0; base && k < textureBanks[0].count; k++) {
            const uint8_t *t = word(at);
            uint32_t rec = at - base, size = rd32(t + 0x14), texels = rd32(t + 0x1C) - base;
            uint32_t pal = rd32(t + 0x20), end = rec + size;
            size_t bytes = ((size_t)rd16(t + 0x0C) * rd16(t + 0x0E) << rd16(t + 0x1A)) / 2;
            if (pal != 0 && pal - base > texels && pal - base <= end)
                end = pal - base;
            /* The index does not hash texels that do not fit; the count of those is checked in C. */
            if (rd16(t + 0x1A) <= 3 && bytes != 0 && texels <= end && bytes <= end - texels) {
                if (!has_at(b, g64_rombanks_has_texels_hash, d, n, texels, bytes))
                    return 0;
                loadedTexels++;
            }
            at += size;
        }
        return 1;
    } else {
        uint32_t base = objectBanks[0].data;
        const uint8_t *e = word(objectBanks[0].entries);
        uint8_t *seen = calloc(n / 8 + 1, 1);
        uint32_t *stack = malloc((n / 4 + 1) * sizeof *stack);
        size_t top = 0;
        int ok = seen && stack;

        for (k = 0; ok && base && k < objectBanks[0].count; k++) {
            stack[top++] = rd32(word(rd32(e + 8 * k + 4)) + 12);
            while (ok && top) {
                uint32_t obj = stack[--top], off = obj - base, mesh, list, child, sibling;
                const uint8_t *o;
                if (off > n || 64 > n - off) {
                    ok = 0;
                    break;
                }
                if (seen[off >> 3] & (1 << (off & 7)))
                    continue;
                seen[off >> 3] |= (uint8_t)(1 << (off & 7));
                o = word(obj);
                mesh = rd32(o + 0x14);
                list = rd32(o + 0x18);
                child = rd32(o + 0x34);
                sibling = rd32(o + 0x38);
                if (mesh) {
                    const uint8_t *m = word(mesh);
                    int16_t numVerts = (int16_t)rd16(m + 2);
                    if (numVerts > 0) {
                        if (!has_at(b, g64_rombanks_has_verts_hash, d, n, rd32(m + 4) - base, (size_t)numVerts * 12))
                            ok = 0;
                        loadedVerts++;
                    }
                }
                /* The loaded list, as the port's RestoreDrawList steps through it. */
                while (ok && list && list - base <= n - 8) {
                    const uint8_t *c = word(list);
                    uint32_t w0 = rd32(c), op = w0 >> 24;
                    if (op == 0xB8)
                        break;
                    if (op == 0x04) {
                        size_t len = ((w0 >> 10) & 0x3F) * 16;
                        if (len) {
                            if (!has_at(b, g64_rombanks_has_vtx_hash, d, n, rd32(c + 4) - base, len))
                                ok = 0;
                            loadedVtx++;
                        }
                        list += 8;
                    } else if (op == 0xFD) {
                        uint32_t next = rd32(c + 8) >> 24;
                        list += next == 0xF5 ? 0x38 : next == 0xE8 ? 0x30 : 0x10;
                    } else {
                        list += 8;
                    }
                }
                if (ok && list && list - base > n - 8)
                    ok = 0;
                if (sibling && top <= n / 4)
                    stack[top++] = sibling;
                if (child && top <= n / 4)
                    stack[top++] = child;
            }
        }
        free(seen);
        free(stack);
        return ok;
    }
}

/* B for one bank: the ids that the game loaded, against the index of that bank. */
static void check_bank(const G64RomBanks *all, const uint8_t *rom, size_t romSize, int kind, unsigned num,
                       unsigned *banks, unsigned *ids) {
    One one = {kind, num};
    G64RomBanks *b = g64_rombanks_build(rom, romSize, one_range, &one);
    G64RomBanksStats st;
    const uint32_t *want;
    uint32_t *got;
    size_t nWant, nGot = 0, i;
    int32_t k;
    const char *what = kind == G64_BANK_TEXTURE ? "texture" : "object";

    if (b == NULL) {
        printf("FAIL %s bank %u: out of memory\n", what, num);
        failed = 1;
        return;
    }
    g64_rombanks_stats(b, &st);
    if (st.skipped[kind] || st.banks[kind] != 1) {
        printf("FAIL %s bank %u: the index skips it\n", what, num);
        failed = 1;
        g64_rombanks_free(b);
        return;
    }
    want = kind == G64_BANK_TEXTURE ? g64_rombanks_texture_ids(b, &nWant) : g64_rombanks_object_ids(b, &nWant);
    if (kind == G64_BANK_TEXTURE) {
        const uint8_t *p = word(textureBanks[0].textures);
        got = malloc(((size_t)textureBanks[0].count + 1) * sizeof *got);
        for (k = 0; got && textureBanks[0].data && k < textureBanks[0].count; k++) {
            int32_t size;
            got[nGot++] = *(const uint32_t *)p;
            memcpy(&size, p + 0x14, 4);
            p += size;
        }
    } else {
        const uint8_t *e = word(objectBanks[0].entries);
        got = malloc(((size_t)objectBanks[0].count + 1) * sizeof *got);
        for (k = 0; got && objectBanks[0].data && k < objectBanks[0].count; k++)
            got[nGot++] = *(const uint32_t *)(e + 8 * k);
    }
    if (got == NULL) {
        printf("FAIL %s bank %u: out of memory\n", what, num);
        failed = 1;
        g64_rombanks_free(b);
        return;
    }
    nGot = unique(got, nGot);
    if (nGot != nWant || (nGot && memcmp(got, want, nGot * sizeof *got) != 0)) {
        printf("FAIL %s bank %u: the game loaded %lu ids and the index lists %lu, or other ones\n", what, num,
               (unsigned long)nGot, (unsigned long)nWant);
        failed = 1;
    }
    {
        uint8_t *buf;
        size_t n;
        const uint8_t *d = bank_bytes(rom, romSize, kind, num, &n, &buf);
        if (d == NULL || !check_loaded_hashes(b, kind, d, n)) {
            printf("FAIL %s bank %u: the index lacks a hash of what the game loaded\n", what, num);
            failed = 1;
        }
        free(buf);
    }
    for (i = 0; i < nWant; i++) {
        uint32_t slot = 0;
        if (!(kind == G64_BANK_TEXTURE ? g64_rombanks_has_texture_id(all, want[i])
                                       : g64_rombanks_has_object_id(all, want[i]))) {
            printf("FAIL %s bank %u: the index of the ROM does not have id number %lu\n", what, num, (unsigned long)i);
            failed = 1;
            break;
        }
        if (kind == G64_BANK_TEXTURE)
            FindTexture(&slot, want[i], 0);
        else
            FindObject(&slot, want[i], "rombanks", 0);
        if (slot == 0) {
            printf("FAIL %s bank %u: the game does not find id number %lu of the index\n", what, num,
                   (unsigned long)i);
            failed = 1;
            break;
        }
    }
    (*banks)++;
    *ids += (unsigned)nWant;
    free(got);
    g64_rombanks_free(b);
}

/* One hash query: the index must find the bytes, and not a copy with its first byte changed. */
static void check_hash(const G64RomBanks *b, int (*has)(const G64RomBanks *, const uint8_t *), const uint8_t *p,
                       size_t n, const char *what) {
    uint8_t h[16], *copy = malloc(n);
    int found, changed = 1;

    g64_rombanks_hash(h, p, n);
    found = has(b, h);
    if (copy) {
        memcpy(copy, p, n);
        copy[0] ^= 0xFF;
        g64_rombanks_hash(h, copy, n);
        changed = has(b, h);
        free(copy);
    }
    printf("%s C: the index finds %s, and not a changed copy of it\n", found && !changed ? "ok  " : "FAIL", what);
    if (!found || changed)
        failed = 1;
}

/* C: the hash queries on the first texture of the first texture bank, and on the first vertex array and G_VTX
 * block of the first objects of the object banks, in bank order. */
static void check_hashes(const G64RomBanks *b, const uint8_t *rom, size_t romSize) {
    unsigned num;
    int texels = 0, verts = 0, vtx = 0;

    for (num = 0; num < 256 && !texels; num++) {
        uint8_t *buf;
        size_t n;
        const uint8_t *d = bank_bytes(rom, romSize, G64_BANK_TEXTURE, num, &n, &buf);
        if (d && n >= 40 && be32(d) > 0) {
            const uint8_t *t = d + 4;
            size_t bytes = (((size_t)(t[12] << 8 | t[13]) * (t[14] << 8 | t[15])) << (t[0x1B] & 3)) / 2;
            if (bytes && 40 + bytes <= n) {
                check_hash(b, g64_rombanks_has_texels_hash, t + 36, bytes, "a texture's texels");
                texels = 1;
            }
        }
        free(buf);
    }
    for (num = 0; num < 256 && !(verts && vtx); num++) {
        uint8_t *buf;
        size_t n, e;
        const uint8_t *d = bank_bytes(rom, romSize, G64_BANK_OBJECT, num, &n, &buf);
        for (e = 0; d && e + 8 <= n && be32(d + e + 4) && !(verts && vtx); e += 8) {
            size_t inst = be32(d + e + 4) & 0xFFFFFF, obj, mesh, list;
            if (inst + 28 > n || (obj = be32(d + inst + 12) & 0xFFFFFF) + 64 > n)
                break;
            mesh = be32(d + obj + 0x14) & 0xFFFFFF;
            list = be32(d + obj + 0x18) & 0xFFFFFF;
            if (!verts && mesh && mesh + 36 <= n) {
                size_t count = (size_t)(d[mesh + 2] << 8 | d[mesh + 3]), at = be32(d + mesh + 4) & 0xFFFFFF;
                if (count > 0 && count < 0x8000 && at + count * 12 <= n) {
                    check_hash(b, g64_rombanks_has_verts_hash, d + at, count * 12, "a vertex array");
                    verts = 1;
                }
            }
            /* The first G_VTX before any texture load of the list. */
            for (; !vtx && list && list + 8 <= n; list += 8) {
                uint32_t op = d[list];
                if (op == 0x04) {
                    size_t at = be32(d + list + 4) & 0xFFFFFF, len = ((be32(d + list) >> 10) & 0x3F) * 16;
                    if (len && at + len <= n) {
                        check_hash(b, g64_rombanks_has_vtx_hash, d + at, len, "a G_VTX block");
                        vtx = 1;
                    }
                    break;
                }
                if (op == 0xB8 || op == 0xFD)
                    break;
            }
        }
        free(buf);
    }
    if (!texels || !verts || !vtx) {
        printf("FAIL C: the ROM's banks have no texture, vertex array or G_VTX block to ask for\n");
        failed = 1;
    }
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    void *boot;
    const uint8_t *rom;
    unsigned long romSize = 0;
    unsigned num, streams = 0, bytes = 0, banks[2] = {0, 0}, ids[2] = {0, 0};
    int kind;
    G64RomBanks *index;
    G64RomBanksStats st;
    clock_t t0, t1;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;

    if (!f) {
        printf("skipped: no ROM (rombanks ROM)\n");
        return 0;
    }
    fclose(f);
    engine_default_options(&opt);
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(e));
    if (!boot) {
        printf("FAIL out of memory\n");
        return 1;
    }
    engine_save(e, boot);
    rom = engine_rom(e, &romSize);

    /* A. The reader against the game's decompressor, on every bank that is an FLA2 stream. */
    for (kind = 0; kind < 2; kind++) {
        for (num = 0; num < 256; num++) {
            uint32_t start, end;
            size_t size;
            uint8_t *mine, *game;
            const char *why;
            int32_t n;
            if (!range_of(kind, num, &start, &end) || end > romSize || start >= end ||
                !g64_fla2_is(rom + start, end - start))
                continue;
            why = g64_fla2_size(rom + start, end - start, 1u << 24, &size);
            mine = why ? NULL : malloc(size ? size : 1);
            if (why == NULL && mine == NULL)
                why = "out of memory";
            if (why == NULL)
                why = g64_fla2_decompress(rom + start, end - start, mine, size);
            if (why) {
                printf("FAIL %s bank %u: %s\n", kind ? "object" : "texture", num, why);
                failed = 1;
                free(mine);
                continue;
            }
            engine_load(e, boot);
            game = JallocAlloc((int32_t)size, 0, "rombanks");
            if (game == NULL) {
                printf("FAIL %s bank %u: no room on the game's heap\n", kind ? "object" : "texture", num);
                failed = 1;
                free(mine);
                continue;
            }
            n = DecompressBuffer((uint8_t *)rom + start + 8, game);
            if ((size_t)n != size || memcmp(mine, game, size) != 0) {
                printf("FAIL %s bank %u: the reader and the game's decompressor give other bytes\n",
                       kind ? "object" : "texture", num);
                failed = 1;
            }
            streams++;
            bytes += (unsigned)size;
            free(mine);
        }
    }
    printf("%s A: %u FLA2 banks (%u bytes) decompress as the game does\n", failed ? "FAIL" : "ok  ", streams, bytes);

    /* C, first part: the index of the whole ROM, which B also asks. */
    t0 = clock();
    index = g64_rombanks_build_game(rom, romSize);
    t1 = clock();
    if (index == NULL) {
        printf("FAIL the index: out of memory\n");
        return 1;
    }

    /* B. Each bank loaded alone in the game, against an index of that bank. */
    for (kind = 0; kind < 2; kind++) {
        int before = failed;
        for (num = 0; num < 256; num++) {
            uint32_t start, end;
            if (!range_of(kind, num, &start, &end))
                continue;
            engine_load(e, boot);
            numCyclingTextures = 0;
            if (kind == G64_BANK_TEXTURE) {
                memset(textureBanks, 0, sizeof textureBanks);
                LoadTextureBank(num);
            } else {
                memset(objectBanks, 0, sizeof objectBanks);
                LoadObjectBank(num);
            }
            check_bank(index, rom, romSize, kind, num, &banks[kind], &ids[kind]);
        }
        printf("%s B: %u %s banks; the game loads and finds the %u ids the index lists\n",
               failed != before ? "FAIL" : "ok  ", banks[kind], kind ? "object" : "texture", ids[kind]);
    }
    printf("%s B: the index has the hashes of the %u texel arrays, %u vertex arrays and %u G_VTX blocks the game "
           "loaded\n", failed ? "FAIL" : "ok  ", loadedTexels, loadedVerts, loadedVtx);
    engine_load(e, boot);

    /* C. The index of the whole ROM. */
    check_hashes(index, rom, romSize);
    g64_rombanks_stats(index, &st);
    if (st.skipped[0] || st.skipped[1]) {
        printf("FAIL the index skips %u texture banks and %u object banks\n", st.skipped[0], st.skipped[1]);
        failed = 1;
    }
    printf("ok   C: %u texture banks and %u object banks, %u of them compressed\n", st.banks[0], st.banks[1],
           st.compressed);
    printf("ok   C: %u object ids, %u texture ids\n", st.objectIds, st.textureIds);
    printf("ok   C: %u texel hashes, %u vertex array hashes, %u G_VTX block hashes; %u textures not hashed\n",
           st.texels, st.verts, st.vtx, st.unhashedTextures);
    {
        /* D. The table against the game's switch. */
        G64RomImage img;
        unsigned same = 0;
        img.rom = rom;
        img.size = romSize;
        for (kind = 0; kind < 2; kind++) {
            for (num = 0; num < 256; num++) {
                uint32_t s0 = 0, e0 = 0, s1 = 0, e1 = 0, s2, e2;
                int game = range_of(kind, num, &s0, &e0), table = g64_bank_rom_range(&img, kind, num, &s1, &e1);
                if (game != table || s0 != s1 || e0 != e1 || g64_bank_exists(NULL, kind, num, &s2, &e2) != game) {
                    printf("FAIL D: %s bank %u: the table and the game's switch differ\n", kind ? "object" : "texture",
                           num);
                    failed = 1;
                } else if (game) {
                    same++;
                }
            }
        }
        printf("%s D: the table of the bank switch gives the game's %u ranges\n", failed ? "FAIL" : "ok  ", same);
    }
    fprintf(stderr, "building the index took %.0f ms\n", (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC);
    g64_rombanks_free(index);
    printf("%s the index of the ROM's banks lists what the game loads\n", failed ? "FAIL" : "ok  ");
    free(boot);
    engine_free(e);
    return failed != 0;
}
