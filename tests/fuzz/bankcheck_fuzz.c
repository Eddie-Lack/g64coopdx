/* A libFuzzer target for the check of a pack's models.bin (src/client/bankcheck.h).
 *
 * The check must never read outside the file, and every bank it accepts must load in the game's loaders without an
 * access outside the bank. Each input is used three ways:
 *   - as a file of its own
 *   - as changes to the cube, Glovebox's real output (tests/fixtures/cube.models.bin, read at start-up from the
 *     folder the target runs in): up to 8 bytes set at chosen offsets
 *   - as the shape of a file made with tests/net/bankbuild.h (objects, trees, textures, G_VTX batches, flags), then
 *     up to 4 bytes changed
 * Every file the check accepts goes through the shadow walk (bankwalk.h), and the rules that need the ROM run on it
 * with an index of a made-up ROM that holds the cube's banks. A fault in the shadow walk stops the run.
 *
 * At the end the target prints how many files the check accepted, and how many of those differ from the cube.
 *
 * Build and run: make fuzz-bankcheck (clang with -fsanitize=fuzzer). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/bankcheck.h"
#include "../../src/client/rombanks.h"
#include "../net/bankbuild.h"
#include "bankwalk.h"

static uint8_t fixture[4096];
static size_t fixtureN;
static G64RomBanks *rom;
static unsigned long accepted, acceptedOther, shapes;

static int game_banks(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    (void)user;
    *start = *end = 0;
    if (kind == G64_BANK_TEXTURE)
        return num <= 18;
    return num <= 71 && num != 16 && num != 22 && num != 24 && num != 28 && num != 32 && num != 34 &&
           (num < 36 || num > 37) && (num < 41 || num > 42) && num != 48 && (num < 50 || num > 51) && num != 57 &&
           (num < 59 || num > 60);
}

static int cube_range(void *user, int kind, unsigned num, uint32_t *start, uint32_t *end) {
    (void)user;
    if (num != 0)
        return 0;
    *start = kind == G64_BANK_TEXTURE ? 16 : 192;
    *end = kind == G64_BANK_TEXTURE ? 192 : 1248;
    return 1;
}

static void report(void) {
    fprintf(stderr, "bankcheck_fuzz: %lu files accepted, %lu of them other than the cube, %lu made from shapes\n",
            accepted, acceptedOther, shapes);
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    FILE *f = fopen("tests/fixtures/cube.models.bin", "rb");
    (void)argc;
    (void)argv;
    if (f) {
        fixtureN = fread(fixture, 1, sizeof fixture, f);
        fclose(f);
    }
    /* Without the fixture the run would skip two of its three modes and still pass. */
    if (fixtureN != 1248) {
        fprintf(stderr, "bankcheck_fuzz: tests/fixtures/cube.models.bin is missing or isn't 1248 bytes\n");
        abort();
    }
    rom = g64_rombanks_build(fixture, fixtureN, cube_range, NULL);
    if (!rom) {
        fprintf(stderr, "bankcheck_fuzz: the index of the fixture's banks couldn't be built\n");
        abort();
    }
    atexit(report);
    return 0;
}

/* Checks a file in a block of exactly its size. An accepted file goes through the shadow walk. */
static void check(const uint8_t *data, size_t n) {
    uint8_t *f = malloc(n ? n : 1);
    G64Models m;
    size_t at = 0;
    const char *why;

    if (f == NULL)
        return;
    memcpy(f, data, n);
    why = g64_models_check(f, n, game_banks, NULL, &m, &at);
    if (why != NULL && at > n)
        __builtin_trap();
    if (why == NULL) {
        accepted++;
        if (n != fixtureN || memcmp(f, fixture, n) != 0)
            acceptedOther++;
        if (m.texSize) {
            int bank = 0;
            const char *walk = g64_bank_shadow(f + m.texAt, m.texSize, f + m.objAt, m.objSize, &at, &bank);
            if (walk) {
                fprintf(stderr, "the shadow walk stopped on an accepted file: %s in bank %d at %u\n", walk, bank,
                        (unsigned)at);
                __builtin_trap();
            }
        }
        if (rom) {
            why = g64_models_check_rom(f, n, rom, &at);
            if (why != NULL && at > n)
                __builtin_trap();
        }
    }
    free(f);
}

/* Up to max changes of one byte, as triples of offset (two bytes) and value. */
static void edit(uint8_t *f, size_t n, const uint8_t *d, size_t size, unsigned max) {
    size_t i;
    for (i = 0; i + 3 <= size && i / 3 < max; i += 3)
        f[((size_t)d[i] << 8 | d[i + 1]) % n] = d[i + 2];
}

static uint8_t byte(const uint8_t *d, size_t size, size_t *pos) {
    return *pos < size ? d[(*pos)++] : 0;
}

/* A file made from the input's first bytes. Returns the bytes of the input it used. */
static size_t shape(const uint8_t *d, size_t size, uint8_t *out, size_t cap, size_t *n) {
    static BB b;
    static const uint16_t flags[] = {0, 2, 4};
    static const uint8_t rtex[] = {3, 9}, robj[] = {12, 40};
    size_t pos = 0;
    unsigned ntex = 1 + byte(d, size, &pos) % 3, entries = 1 + byte(d, size, &pos) % 3, t, e;

    bb_begin(&b, entries);
    for (t = 0; t < ntex; t++) {
        unsigned sw = 1 + byte(d, size, &pos) % 8, sh = 1 + byte(d, size, &pos) % 8;
        while (sw + sh > 11)
            sw > sh ? sw-- : sh--;
        bb_texture(&b, 0x0A000000u + t, 1u << sw, 1u << sh, byte(d, size, &pos) & 1);
    }
    for (e = 0; e < entries; e++) {
        unsigned count = 1 + byte(d, size, &pos) % 4, k, links = byte(d, size, &pos);
        uint32_t made[4] = {0, 0, 0, 0};
        BBObject s;
        for (k = 0; k < count; k++) {
            memset(&s, 0, sizeof s);
            s.name = "piece";
            s.flags = flags[byte(d, size, &pos) % 3];
            s.verts = 3 + byte(d, size, &pos) % 40;
            s.faces = 1 + byte(d, size, &pos) % 30;
            s.ntex = e == 0 && k == 0 ? ntex : 1 + byte(d, size, &pos) % ntex;
            for (t = 0; t < s.ntex; t++)
                s.tex[t] = 0x0A000000u + (e == 0 && k == 0 ? t : byte(d, size, &pos) % ntex);
            s.batches = 1 + byte(d, size, &pos) % 3;
            s.pos[0] = (float)(int8_t)byte(d, size, &pos) * 100.0f;
            s.scale[2] = (float)(byte(d, size, &pos) % 64 + 1) / 4.0f;
            /* The object made before this one becomes its child or its sibling. */
            if (k > 0 && ((links >> k) & 1))
                s.sibling = made[k - 1];
            else if (k > 0)
                s.child = made[k - 1];
            made[k] = bb_object(&b, &s);
        }
        bb_entry(&b, 0x0B000000u + e, made[count - 1], byte(d, size, &pos) & 1);
    }
    *n = bb_end(&b, out, cap, 0, rtex, byte(d, size, &pos) % 3, robj, byte(d, size, &pos) % 3);
    return pos;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static uint8_t made[sizeof(((BB *)0)->tex) + sizeof(((BB *)0)->obj) + 64];
    size_t n = 0, used;

    check(data, size);
    if (size == 0)
        return 0;
    if (fixtureN && (data[0] & 1)) {
        uint8_t f[sizeof fixture];
        memcpy(f, fixture, fixtureN);
        edit(f, fixtureN, data + 1, size - 1, 8);
        check(f, fixtureN);
    } else {
        used = shape(data + 1, size - 1, made, sizeof made, &n);
        if (n) {
            shapes++;
            edit(made, n, data + 1 + used, size - 1 - used, 4);
            check(made, n);
        }
    }
    return 0;
}
