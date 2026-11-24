/* The index of the ROM's banks (src/client/rombanks.c) and the bounded FLA2 reader (src/client/fla2.c) against the
 * game, with the ROM.
 *
 *   A. For every bank of the ROM that is an FLA2 stream, the reader gives the same bytes as the game's own
 *      decompressor (DecompressBuffer).
 *   B. For every texture bank and object bank, the game loads the bank alone (LoadTextureBank or LoadObjectBank).
 *      An index of that bank alone must list exactly the ids the loaded bank has, and FindTexture or FindObject
 *      must find each of them.
 *   C. The index of the whole ROM skips no bank. It prints its counts. The time it took goes to stderr.
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

/* B for one bank: the ids that the game loaded, against the index of that bank. */
static void check_bank(const uint8_t *rom, size_t romSize, int kind, unsigned num, unsigned *banks, unsigned *ids) {
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
    for (i = 0; i < nWant; i++) {
        uint32_t slot = 0;
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
            check_bank(rom, romSize, kind, num, &banks[kind], &ids[kind]);
        }
        printf("%s B: %u %s banks; the game loads and finds the %u ids the index lists\n",
               failed != before ? "FAIL" : "ok  ", banks[kind], kind ? "object" : "texture", ids[kind]);
    }
    engine_load(e, boot);

    /* C. The index of the whole ROM. */
    t0 = clock();
    index = g64_rombanks_build_game(rom, romSize);
    t1 = clock();
    if (index == NULL) {
        printf("FAIL the index: out of memory\n");
        return 1;
    }
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
    fprintf(stderr, "building the index took %.0f ms\n", (double)(t1 - t0) * 1000.0 / CLOCKS_PER_SEC);
    g64_rombanks_free(index);
    printf("%s the index of the ROM's banks lists what the game loads\n", failed ? "FAIL" : "ok  ");
    free(boot);
    engine_free(e);
    return failed != 0;
}
