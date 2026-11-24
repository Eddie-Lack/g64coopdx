/* A level pack's own banks in the real game, with the ROM (docs/discovery/model-banks.md, step 4).
 *
 * The relocation proof. The bank check (src/client/bankcheck.c) and its shadow walk (tests/fuzz/bankwalk.c) are
 * copies of what the game's loaders do. Here the game itself loads each bank through G64LoadPackBanks, in a real
 * level. For each bank:
 *   - the check accepts it, and the shadow walk ends without a fault
 *   - the port's own converter (port_convert_texture_bank and port_convert_object_bank) gives the same bytes as the
 *     shadow walk's converter
 *   - the words that the game's loaders changed are the words that the shadow walk changed, in both banks. A word
 *     changed is one that differs between the converted bank and the loaded bank in the game's memory
 *   - each changed word holds what the shadow walk wrote, once the shadow's bases are replaced by the bank's address
 * The level file names none of the pack's objects in these runs, so nothing but the loaders writes into the banks.
 *
 * The play. The same banks again, with the level file's copy naming each object of the pack's directory as scenery
 * (a BACKGROUND token, which makes no collision). Two players press buttons at random for FRAMES frames. The level
 * must play, and two runs must end with the same memory hash.
 *
 * The heap. For every level with 16 players: the free heap where the pack's banks load, and the smallest free heap
 * and block count while the level starts and runs. Then a pack of banks at the size limit (4 MiB together, meshes
 * of 1500 faces as scenery) at the level with the least room, and at the level with the fewest free blocks. The
 * level must play or stop as unplayable (game mode 13), and the program must not stop.
 *
 * The test's banks are made here (tests/net/bankbuild.h), and the fixture is Glovebox's output on a model drawn from
 * scratch. The level files are copies of the ROM's own, in memory only. The output holds no address, and make
 * bankwalk compares it between Linux and Wine.
 *
 * Usage: bankwalk ROM [FIXTURE [FRAMES]]. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The largest bank the builder makes: the size limit of the two banks together. */
#define BB_BANK (4 << 20)
#include "../net/bankbuild.h"
#include "../fuzz/bankwalk.h"
#include "../../src/client/bankcheck.h"
#include "../../src/client/levcheck.h"
#include "../../weemitt/bruteforce/engine/engine.h"

typedef struct {
    unsigned levels, allGaribs;
    unsigned char hubTarget, hubLevel, worldComplete, raisedBit;
} G64Save;
extern int g64Online;
extern unsigned short g64Buttons[16];
extern signed char g64StickX[16], g64StickY[16];
extern int g64PackHeapFree, g64PackHeapGap, g64PackHeapBlocks, g64PackHeapLastStatic;
extern int g64PlanesBlocks, g64PlanesLastStatic;
extern long long g64PlanesBytes, g64PlanesWork;
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
void G64SetStartRecord(int world, const G64Save *s);
void G64SetPack(int level, unsigned rom, unsigned size);
void G64SetPackModels(unsigned texRom, unsigned texSize, unsigned objRom, unsigned objSize);
const void *G64PackBankLoaded(int objects, unsigned *word);
void G64LoadPackBanks(int num);
unsigned G64LevelFileRom(int num, unsigned *size);
int G64LevelUnplayable(void);
int JallocFreeBlocks(void);
void port_convert_texture_bank(uint8_t *bank, uint32_t size);
void port_convert_object_bank(uint8_t *bank, uint32_t size);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);
/* The game's heap control (weemitt's include/jalloc.h): its size and the bytes used, after the table of blocks. */
extern unsigned char jallocControl[];

enum { LEVELS = 48, LEVEL = 13, QUIET = 300, FIRST_SCENE = 43, T_BACKGROUND = 145, T_LANDSCAPE = 146, T_END = 0x7D00 };
#define BOT_BUTTONS 0xE01Fu

static Engine *engine;
static void *boot;
static const uint8_t *rom;
static unsigned long romSize;
static unsigned frames = 600;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
    failed |= !ok;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint32_t get32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static int heap_free(void) {
    int size, used, dyn;
    memcpy(&size, jallocControl + 0x6404, 4);
    memcpy(&used, jallocControl + 0x6410, 4);
    memcpy(&dyn, jallocControl + 0x6414, 4);
    return size - used - dyn;
}

/* ---- A level file with the pack's objects in it. */

static size_t endAt;
static int sawEnd;
/* The token level_file puts in for each object: scenery (BACKGROUND), or landscape (LANDSCAPE, with collision). */
static int fileToken = T_BACKGROUND;

static void find_end(void *user, size_t offset, int token) {
    (void)user;
    if (token == T_END) {
        endAt = offset;
        sawEnd = 1;
    }
}

/* A copy of level's file from the ROM, in a new buffer. With ids, a BACKGROUND token for each of them is put before
   the end token. Returns NULL when the ROM has no such file or the copy does not pass the level check. */
static uint8_t *level_file(int level, const uint32_t *ids, unsigned count, size_t *n) {
    unsigned size = 0, at = G64LevelFileRom(level, &size), i;
    size_t at2 = 0, grown;
    uint8_t *lev;
    if (at == 0 || size < 8 || at + size > romSize)
        return NULL;
    sawEnd = 0;
    if (g64_lev_check(rom + at, size, find_end, NULL, &at2) != NULL || !sawEnd)
        return NULL;
    grown = size + (size_t)count * 26;
    lev = calloc(1, grown);
    if (!lev)
        return NULL;
    memcpy(lev, rom + at, endAt);
    for (i = 0; i < count; i++) {
        uint8_t *t = lev + endAt + 26 * i;
        bb_put16(t, (uint16_t)fileToken);
        bb_put32(t + 2, ids[i]);
        memcpy(t + 6, "pack", 4);
        /* x, y and z: the level's origin, with the pieces 40 units apart. */
        bb_putf(t + 14, 40.0f * (float)(i % 8));
        bb_putf(t + 18, 0.0f);
        bb_putf(t + 22, 40.0f * (float)(i / 8));
    }
    memcpy(lev + endAt + 26 * count, rom + at + endAt, size - endAt);
    bb_put32(lev, (uint32_t)grown);
    if (g64_lev_check(lev, grown, NULL, NULL, &at2) != NULL) {
        free(lev);
        return NULL;
    }
    *n = grown;
    return lev;
}

/* ---- A game with the pack. */

static unsigned seed;

static unsigned next(void) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

static uint8_t *withBuf;

/* Starts level with the pack's level file lev and the two banks of models.bin, with players players. */
static void start(int level, const uint8_t *lev, size_t levN, const uint8_t *file, const G64Models *m, int players) {
    G64Save s;
    unsigned long total = romSize + levN + m->texSize + m->objSize;
    unsigned mask = players >= 16 ? 0xFFFFu : (1u << players) - 1;
    free(withBuf);
    withBuf = malloc(total);
    if (!withBuf) {
        printf("FAIL out of memory\n");
        exit(1);
    }
    memcpy(withBuf, rom, romSize);
    memcpy(withBuf + romSize, lev, levN);
    memcpy(withBuf + romSize + levN, file + m->texAt, m->texSize);
    memcpy(withBuf + romSize + levN + m->texSize, file + m->objAt, m->objSize);
    engine_load(engine, boot);
    engine_set_rom(engine, withBuf, total);
    G64SetPack(level, (unsigned)romSize, (unsigned)levN);
    G64SetPackModels((unsigned)(romSize + levN), m->texSize, (unsigned)(romSize + levN + m->texSize), m->objSize);
    g64Online = 1;
    G64StartPlayers(mask);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    /* Not a count: the planes test did not run. */
    g64PlanesBlocks = -1;
    port_game_start_level(level);
}

/* The game and the test without the pack again. */
static void stop(void) {
    G64SetPack(-1, 0, 0);
    G64SetPackModels(0, 0, 0, 0);
    engine_load(engine, boot);
    engine_set_rom(engine, rom, romSize);
}

/* Runs the game with players who press buttons at random, as tests/rom/bots.c does, with its seed for the level.
   Other inputs can crash the game in level 14 with 16 players: a frog hand's tongue with no bullet (MaintainAction,
   hand.c), a fault of the game that has nothing to do with packs. Gives the smallest free heap and block count seen
   when minFree and minBlocks are not NULL. */
static int stoppedSeen;

static void play(int players, unsigned count, int level, int *minFree, int *minBlocks) {
    unsigned short buttons[16];
    signed char sx[16], sy[16];
    unsigned mask = players >= 16 ? 0xFFFFu : (1u << players) - 1;
    unsigned f;
    int p;
    memset(buttons, 0, sizeof buttons);
    memset(sx, 0, sizeof sx);
    memset(sy, 0, sizeof sy);
    seed = 12345u + (unsigned)level;
    for (f = 0; f < count; f++) {
        EngineInput in;
        G64SetPresent(mask);
        for (p = 0; p < 16; p++) {
            if (p >= players) {
                g64Buttons[p] = 0;
                g64StickX[p] = g64StickY[p] = 0;
                continue;
            }
            if (f < QUIET) {
                buttons[p] = f % 30 == 0 ? 0x8000 : 0;
            } else if (next() % 12 == 0) {
                sx[p] = (signed char)((int)(next() % 161) - 80);
                sy[p] = (signed char)((int)(next() % 161) - 80);
                buttons[p] = next() % 3 == 0 ? (unsigned short)(next() & BOT_BUTTONS) : 0;
            }
            g64Buttons[p] = buttons[p];
            g64StickX[p] = sx[p];
            g64StickY[p] = sy[p];
        }
        memset(&in, 0, sizeof in);
        in.buttons = g64Buttons[0];
        in.x = g64StickX[0];
        in.y = g64StickY[0];
        engine_step(engine, &in);
        stoppedSeen |= G64LevelUnplayable();
        if (minFree && heap_free() < *minFree)
            *minFree = heap_free();
        if (minBlocks && JallocFreeBlocks() < *minBlocks)
            *minBlocks = JallocFreeBlocks();
    }
}

/* ---- The proof. */

/* Compares the game's loaded bank with the shadow walk's. conv is the port's converter on a copy of the raw bank,
   and loaded the bank in the game's memory at address word. Returns the number of changed words, or -1 when the
   two disagree. */
static long compare(const char *what, const uint8_t *conv, const uint8_t *loaded, uint32_t word, const uint8_t *sConv,
                    const uint8_t *sLoaded, size_t n, uint32_t objWord, size_t objN, uint32_t texWord, size_t texN) {
    size_t i;
    long changed = 0;
    char line[256];
    if (memcmp(conv, sConv, n) != 0) {
        for (i = 0; i < n && conv[i] == sConv[i]; i++) {
        }
        snprintf(line, sizeof line, "%s: the port's converter and the shadow's differ first at byte %lu", what,
                 (unsigned long)i);
        check(0, line);
        return -1;
    }
    (void)word;
    for (i = 0; i + 4 <= n; i += 4) {
        int real = get32(conv + i) != get32(loaded + i), shadow = get32(sConv + i) != get32(sLoaded + i);
        uint32_t v = get32(sLoaded + i), want = v;
        if (real != shadow) {
            snprintf(line, sizeof line, "%s: the word at %lu was changed by %s only", what, (unsigned long)i,
                     real ? "the game" : "the shadow walk");
            check(0, line);
            return -1;
        }
        if (!real)
            continue;
        changed++;
        if (v >= G64_SHADOW_BASE && v - G64_SHADOW_BASE < objN)
            want = objWord + (v - G64_SHADOW_BASE);
        else if (v >= G64_SHADOW_TEXBASE && v - G64_SHADOW_TEXBASE < texN)
            want = texWord + (v - G64_SHADOW_TEXBASE);
        if (get32(loaded + i) != want) {
            snprintf(line, sizeof line, "%s: the word at %lu holds another value than the shadow walk's", what,
                     (unsigned long)i);
            check(0, line);
            return -1;
        }
    }
    /* The bytes past the last whole word. */
    for (; i < n; i++)
        if ((conv[i] != loaded[i]) != (sConv[i] != sLoaded[i]))
            return -1;
    return changed;
}

/* Checks models.bin, then loads it in level with players players and compares the loads (when proof), then plays it
   with its objects in the level (when frames). Prints one line for each part. */
static void run_case(const char *name, const uint8_t *file, size_t n, int level, int players, int proof,
                     int mayStop) {
    G64Models m;
    G64BankImages im;
    size_t at = 0;
    int bank = 0, k;
    const char *why;
    char line[300];
    uint8_t *tc, *oc;
    uint8_t *lev;
    size_t levN = 0;
    uint32_t ids[1024];
    unsigned nids = 0;

    why = g64_models_check(file, n, NULL, NULL, &m, &at);
    snprintf(line, sizeof line, "%s: the bank check accepts it (%s)", name, why ? why : "no fault");
    check(why == NULL, line);
    if (why)
        return;
    im.texConverted = malloc(m.texSize);
    im.texLoaded = malloc(m.texSize);
    im.objConverted = malloc(m.objSize);
    im.objLoaded = malloc(m.objSize);
    tc = malloc(m.texSize);
    oc = malloc(m.objSize);
    if (!im.texConverted || !im.texLoaded || !im.objConverted || !im.objLoaded || !tc || !oc) {
        printf("FAIL out of memory\n");
        exit(1);
    }
    why = g64_bank_shadow_images(file + m.texAt, m.texSize, file + m.objAt, m.objSize, &im, &at, &bank);
    snprintf(line, sizeof line, "%s: the shadow walk ends without a fault (%s)", name, why ? why : "no fault");
    check(why == NULL, line);
    for (k = 0; why == NULL && k < 1024 && be32(file + m.objAt + 8 * k + 4) != 0; k++)
        ids[nids++] = be32(file + m.objAt + 8 * k);

    if (why == NULL && proof) {
        unsigned texWord = 0, objWord = 0;
        const uint8_t *texLoaded, *objLoaded;
        long tch, och;
        lev = level_file(level, NULL, 0, &levN);
        check(lev != NULL, "the level's file passes the level check");
        if (lev) {
            start(level, lev, levN, file, &m, players);
            texLoaded = G64PackBankLoaded(0, &texWord);
            objLoaded = G64PackBankLoaded(1, &objWord);
            snprintf(line, sizeof line, "%s: the game loads both banks in level %d", name, level);
            check(texLoaded != NULL && objLoaded != NULL && !G64LevelUnplayable(), line);
            if (texLoaded && objLoaded) {
                memcpy(tc, file + m.texAt, m.texSize);
                memcpy(oc, file + m.objAt, m.objSize);
                port_convert_texture_bank(tc, m.texSize);
                port_convert_object_bank(oc, m.objSize);
                tch = compare("texture bank", tc, texLoaded, texWord, im.texConverted, im.texLoaded, m.texSize,
                              objWord, m.objSize, texWord, m.texSize);
                och = compare("object bank", oc, objLoaded, objWord, im.objConverted, im.objLoaded, m.objSize,
                              objWord, m.objSize, texWord, m.texSize);
                snprintf(line, sizeof line,
                         "%s: the game's loaders changed the shadow walk's words: %ld in the texture bank, %ld in the "
                         "object bank",
                         name, tch, och);
                check(tch > 0 && och > 0, line);
            }
            free(lev);
        }
        stop();
    }

    if (why == NULL && frames) {
        unsigned hash[2] = {0, 0};
        int r, stopped[2] = {0, 0}, minFree = 0x7FFFFFFF, minBlocks = 0x7FFFFFFF;
        lev = level_file(level, ids, nids, &levN);
        snprintf(line, sizeof line, "%s: the level's file with the pack's %u objects as scenery passes the level check",
                 name, nids);
        check(lev != NULL, line);
        for (r = 0; lev && r < 2; r++) {
            start(level, lev, levN, file, &m, players);
            stoppedSeen = G64LevelUnplayable();
            if (heap_free() < minFree)
                minFree = heap_free();
            if (JallocFreeBlocks() < minBlocks)
                minBlocks = JallocFreeBlocks();
            if (!stoppedSeen)
                play(players, frames, level, &minFree, &minBlocks);
            stopped[r] = stoppedSeen;
            hash[r] = port_hash_ram();
            stop();
        }
        if (lev) {
            if (mayStop)
                snprintf(line, sizeof line, "%s: level %d with %d players %s, the same in two runs, hash %08x", name,
                         level, players, stopped[0] ? "stops as unplayable" : "plays", hash[0]);
            else
                snprintf(line, sizeof line, "%s: level %d plays %u frames with %d players, the same in two runs, "
                         "hash %08x", name, level, frames, players, hash[0]);
            check(hash[0] == hash[1] && stopped[0] == stopped[1] && (mayStop || !stopped[0]), line);
            printf("%s: at least %d bytes and %d blocks of the heap free\n", name, minFree, minBlocks);
        }
        free(lev);
    }
    free(im.texConverted);
    free(im.texLoaded);
    free(im.objConverted);
    free(im.objLoaded);
    free(tc);
    free(oc);
}

/* With the pack loaded in level 13, the five object slots hold the system's bank, the world's, bank 1, the level's
   and the pack's. A second load of the pack then finds no free slot. It must stop the level before it loads anything. */
static void no_slot(const uint8_t *file, size_t n) {
    G64Models m;
    size_t at = 0, levN = 0;
    uint8_t *lev;
    int before;
    if (g64_models_check(file, n, NULL, NULL, &m, &at) != NULL || (lev = level_file(LEVEL, NULL, 0, &levN)) == NULL) {
        check(0, "the bank for the test of full slots");
        return;
    }
    start(LEVEL, lev, levN, file, &m, 2);
    before = heap_free();
    check(!G64LevelUnplayable(), "a pack's banks load in level 13 and take its last free object slot");
    G64LoadPackBanks(LEVEL);
    check(G64LevelUnplayable() && heap_free() == before,
          "a second load of the pack finds no free slot: the level stops as unplayable, and nothing more is loaded");
    stop();
    free(lev);
}

/* ---- The test's banks. */

static BB bb;
static uint8_t out[(4 << 20) + 64];

static void shape(BBObject *s, unsigned verts, unsigned faces) {
    memset(s, 0, sizeof *s);
    s->verts = verts;
    s->faces = faces;
    s->ntex = 1;
    s->tex[0] = 0x0A000001;
}

static void builder_cases(void) {
    BBObject s;
    uint32_t leaf, mid, root, o[10];
    size_t n;
    unsigned i;

    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 8, 8, 0);
    shape(&s, 8, 12);
    bb_entry(&bb, 0x0B000001, bb_object(&bb, &s), 1);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    run_case("a cube", out, n, LEVEL, 2, 1, 0);

    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 2, 2, 0);
    shape(&s, 6, 4);
    s.name = "b";
    leaf = bb_object(&bb, &s);
    s.name = "a";
    s.sibling = leaf;
    mid = bb_object(&bb, &s);
    s.name = "root";
    s.sibling = 0;
    s.child = mid;
    s.flags = 2;
    root = bb_object(&bb, &s);
    bb_entry(&bb, 0x0B000003, root, 1);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    run_case("a tree of three objects", out, n, LEVEL, 2, 1, 0);

    bb_begin(&bb, 2);
    bb_texture(&bb, 0x0A000001, 64, 32, 0);
    bb_texture(&bb, 0x0A000002, 4, 4, 1);
    shape(&s, 40, 60);
    s.ntex = 2;
    s.tex[0] = 0x0A000002;
    s.tex[1] = 0x0A000001;
    s.batches = 3;
    s.scale[1] = 4.0f;
    bb_entry(&bb, 0x0B000004, bb_object(&bb, &s), 1);
    s.ntex = 1;
    s.batches = 1;
    s.pos[0] = -12.5f;
    s.scale[1] = 0.5f;
    bb_entry(&bb, 0x0B000005, bb_object(&bb, &s), 0);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    run_case("two textures, three G_VTX batches each, two entries", out, n, LEVEL, 2, 1, 0);

    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 16, 16, 0);
    shape(&s, 30, 20);
    s.batches = 8;
    s.reloads = 400;
    bb_entry(&bb, 0x0B000006, bb_object(&bb, &s), 0);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    run_case("a long display list: 400 more texture loads", out, n, LEVEL, 2, 1, 0);

    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 8, 8, 0);
    shape(&s, 3, 1);
    for (i = 0; i < 10; i++) {
        s.child = i ? o[i - 1] : 0;
        o[i] = bb_object(&bb, &s);
    }
    bb_entry(&bb, 0x0B000007, o[9], 0);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    run_case("ten levels of children", out, n, LEVEL, 2, 1, 0);

    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 8, 8, 0);
    shape(&s, 4500, 1500);
    bb_entry(&bb, 0x0B000008, bb_object(&bb, &s), 1);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    run_case("a mesh of 1500 faces and 4500 vertices", out, n, LEVEL, 2, 1, 0);
}

/* Banks at the size limit: one texture of 2048 texels, and as many objects with meshes of 1500 faces and 4500
   vertices as fit in 4 MiB together. */
static size_t largest(void) {
    BBObject s;
    uint32_t before, per;
    unsigned count, i;
    size_t n;
    shape(&s, 4500, 1500);
    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 32, 64, 0);
    before = bb.on;
    bb_entry(&bb, 0x0B100000, bb_object(&bb, &s), 0);
    per = bb.on - before + 8;
    count = (unsigned)((G64_MODELS_BANKS_MAX - ((bb.tn + 4 + 7) & ~7u) - 8) / per);
    for (;; count--) {
        bb_begin(&bb, count);
        bb_texture(&bb, 0x0A000001, 32, 64, 0);
        for (i = 0; i < count; i++)
            bb_entry(&bb, 0x0B100000 + i, bb_object(&bb, &s), 0);
        n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
        if (n && be32(out) + be32(out + 4) <= G64_MODELS_BANKS_MAX)
            break;
    }
    printf("the largest pack: %u objects of 1500 faces, banks of %u and %u bytes\n", count, be32(out), be32(out + 4));
    return n;
}

/* ---- The collision planes.

   After the parse, the game makes collision planes for each landscape and water actor: one more block, 120 bytes a
   face, and for landscape with collision a comparison of every face with every later face. The planes test in the
   parser (G64PlanesFit, parse.c) stops the level when they would not fit or take too long. Each case here puts one
   object in level 13 many times, with 16 players. The largest count that starts must play FRAMES frames the same in
   two runs. One more copy must stop as unplayable at the planes test, and the program must go on. */

/* The level the cases run in. */
static int planesLevel = LEVEL;
static uint32_t landscapeId;
static int sawLandscape;

static void find_landscape(void *user, size_t offset, int token) {
    (void)user;
    if (token == T_LANDSCAPE && !sawLandscape) {
        sawLandscape = 1;
        landscapeId = be32((const uint8_t *)user + offset + 2);
    }
}

/* Starts level 13 with copies copies of id. Returns 1 when the level did not stop. */
static int planes_start(uint32_t id, unsigned copies, const uint8_t *file, const G64Models *m) {
    static uint32_t ids[4096];
    size_t levN = 0;
    uint8_t *lev;
    unsigned i;
    int ok;
    for (i = 0; i < copies; i++)
        ids[i] = id;
    lev = level_file(planesLevel, ids, copies, &levN);
    if (!lev) {
        printf("FAIL the level's file with %u copies does not pass the level check\n", copies);
        failed = 1;
        return 0;
    }
    start(planesLevel, lev, levN, file, m, 16);
    ok = !G64LevelUnplayable();
    free(lev);
    return ok;
}

static void planes_case(const char *name, uint32_t id, const uint8_t *file, const G64Models *m) {
    unsigned lo = 0, hi = 1, mid, r;
    char line[300];
    unsigned hash[2] = {0, 0};
    int stopped[2] = {0, 0}, minFree = 0x7FFFFFFF, minBlocks = 0x7FFFFFFF, blocks;
    long long bytes, work;
    /* lo starts, hi stops. */
    while (planes_start(id, hi, file, m)) {
        lo = hi;
        hi *= 2;
        if (hi > 4096) {
            snprintf(line, sizeof line, "%s: 4096 copies stop the level", name);
            check(0, line);
            stop();
            return;
        }
    }
    while (hi - lo > 1) {
        mid = lo + (hi - lo) / 2;
        if (planes_start(id, mid, file, m))
            lo = mid;
        else
            hi = mid;
    }
    /* One more than the most that start: the planes test ran and stopped it. The game then runs 60 frames, as it
       does after any stopped level, and the program must not end. */
    planes_start(id, hi, file, m);
    blocks = g64PlanesBlocks;
    bytes = g64PlanesBytes;
    work = g64PlanesWork;
    r = (unsigned)G64LevelUnplayable();
    play(16, 60, planesLevel, NULL, NULL);
    snprintf(line, sizeof line, "%s: %u copies stop the level at the planes test (%d blocks, %lld bytes, work %lld), "
             "and the game runs 60 more frames", name, hi, blocks, bytes, work);
    check(r && blocks >= (int)hi, line);
    stop();
    if (lo == 0) {
        snprintf(line, sizeof line, "%s: one copy starts", name);
        check(0, line);
        return;
    }
    for (r = 0; r < 2; r++) {
        clock_t t0 = clock();
        planes_start(id, lo, file, m);
        /* The time of the start goes to stderr: it differs between machines, and make bankwalk compares stdout. */
        if (r == 0)
            fprintf(stderr, "%s: %u copies start in %.2f s\n", name, lo, (double)(clock() - t0) / CLOCKS_PER_SEC);
        blocks = g64PlanesBlocks;
        bytes = g64PlanesBytes;
        work = g64PlanesWork;
        stoppedSeen = G64LevelUnplayable();
        if (heap_free() < minFree)
            minFree = heap_free();
        if (JallocFreeBlocks() < minBlocks)
            minBlocks = JallocFreeBlocks();
        if (!stoppedSeen)
            play(16, frames, planesLevel, &minFree, &minBlocks);
        stopped[r] = stoppedSeen;
        hash[r] = port_hash_ram();
        stop();
    }
    snprintf(line, sizeof line, "%s: %u copies (%d blocks, %lld bytes, work %lld) play %u frames with 16 players, the "
             "same in two runs, hash %08x; at least %d bytes and %d blocks free", name, lo, blocks, bytes, work,
             frames, hash[0], minFree, minBlocks);
    check(!stopped[0] && !stopped[1] && hash[0] == hash[1], line);
}

static void planes_cases(void) {
    BBObject s;
    G64Models m, none;
    size_t n, at = 0, at2 = 0;
    unsigned size = 0, from = G64LevelFileRom(LEVEL, &size);

    /* The block list: a pack's object of one face, and level 13's own first landscape object, as scenery. */
    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 8, 8, 0);
    shape(&s, 3, 1);
    bb_entry(&bb, 0x0B000009, bb_object(&bb, &s), 1);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    if (g64_models_check(out, n, NULL, NULL, &m, &at) != NULL) {
        check(0, "the bank of one face passes the bank check");
        return;
    }
    planes_case("the planes' blocks, a pack's object of one face as scenery", 0x0B000009, out, &m);
    fileToken = T_LANDSCAPE;
    planes_case("the planes' blocks, a pack's object of one face as landscape", 0x0B000009, out, &m);
    fileToken = T_BACKGROUND;
    sawLandscape = 0;
    if (from && from + size <= romSize)
        g64_lev_check(rom + from, size, find_landscape, (void *)(rom + from), &at2);
    check(sawLandscape, "level 13's file has a landscape object");
    memset(&none, 0, sizeof none);
    if (sawLandscape)
        planes_case("the planes' blocks, level 13's own landscape object as scenery, with no pack", landscapeId, out,
                    &none);

    /* The heap: a mesh of 1500 faces as scenery. */
    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 8, 8, 0);
    shape(&s, 4500, 1500);
    bb_entry(&bb, 0x0B00000A, bb_object(&bb, &s), 1);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    if (g64_models_check(out, n, NULL, NULL, &m, &at) != NULL) {
        check(0, "the bank of 1500 faces passes the bank check");
        return;
    }
    planes_case("the planes' bytes, a mesh of 1500 faces as scenery", 0x0B00000A, out, &m);

    /* The work: a flat mesh of 1500 faces as landscape, with collision. */
    shape(&s, 4500, 1500);
    s.flat = 1;
    bb_begin(&bb, 1);
    bb_texture(&bb, 0x0A000001, 8, 8, 0);
    bb_entry(&bb, 0x0B00000B, bb_object(&bb, &s), 1);
    n = bb_end(&bb, out, sizeof out, 0, NULL, 0, NULL, 0);
    if (g64_models_check(out, n, NULL, NULL, &m, &at) != NULL) {
        check(0, "the flat bank of 1500 faces passes the bank check");
        return;
    }
    fileToken = T_LANDSCAPE;
    planes_case("the planes' work, a flat mesh of 1500 faces as landscape", 0x0B00000B, out, &m);
    fileToken = T_BACKGROUND;
}

/* ---- The heap with 16 players. */

/* Finds the level with the least room where the banks load, the one with the fewest free blocks there, and the
   played level (not a scene) with the least room. */
static void heap_survey(int *worstBytes, int *worstBlocks, int *worstPlayed) {
    int level, minGap = 0x7FFFFFFF, minBlocks = 0x7FFFFFFF, minPlayed = 0x7FFFFFFF, maxBlocks = 0, maxTo = 0, any = 0;
    long long maxBytes = 0, maxWork = 0, minLeft = 0x7FFFFFFFFFFFFFFFLL;
    G64Save s;
    for (level = 0; level < LEVELS; level++) {
        int atFree, atGap, atBlocks, atLast, runFree, runBlocks;
        char line[200];
        engine_load(engine, boot);
        g64Online = 1;
        G64StartPlayers(0xFFFFu);
        memset(&s, 0, sizeof s);
        s.levels = 1;
        G64SetStartRecord(0, &s);
        g64PlanesBlocks = -1;
        port_game_start_level(level);
        snprintf(line, sizeof line, "level %d starts with 16 players and no pack", level);
        check(!G64LevelUnplayable(), line);
        /* The collision planes' needs, against the planes test's limits. The free heap after them is the heap at
           the end of the start plus the planes of water, which the game frees again. */
        if (g64PlanesBlocks >= 0) {
            any = 1;
            printf("planes level %d: %d blocks, %lld bytes, work %lld\n", level, g64PlanesBlocks, g64PlanesBytes,
                   g64PlanesWork);
            if (g64PlanesBlocks > maxBlocks)
                maxBlocks = g64PlanesBlocks;
            if (g64PlanesBytes > maxBytes)
                maxBytes = g64PlanesBytes;
            if (g64PlanesWork > maxWork)
                maxWork = g64PlanesWork;
            if (g64PlanesLastStatic + g64PlanesBlocks > maxTo)
                maxTo = g64PlanesLastStatic + g64PlanesBlocks;
            if ((long long)heap_free() < minLeft)
                minLeft = heap_free();
        }
        atFree = g64PackHeapFree;
        atGap = g64PackHeapGap;
        atBlocks = g64PackHeapBlocks;
        atLast = g64PackHeapLastStatic;
        runFree = heap_free();
        runBlocks = JallocFreeBlocks();
        play(16, frames, level, &runFree, &runBlocks);
        printf("heap level %d: at the banks' load %d bytes free, %d between the blocks, %d blocks free, last static "
               "block %d; at least %d bytes and %d blocks free after it\n",
               level, atFree, atGap, atBlocks, atLast, runFree, runBlocks);
        fflush(stdout);
        if (atGap < minGap) {
            minGap = atGap;
            *worstBytes = level;
        }
        if (level < FIRST_SCENE && atGap < minPlayed) {
            minPlayed = atGap;
            *worstPlayed = level;
        }
        if (atBlocks < minBlocks) {
            minBlocks = atBlocks;
            *worstBlocks = level;
        }
    }
    if (any)
        printf("planes: at most %d blocks, %lld bytes and work %lld in a level of the game; the last static block "
               "before the planes plus the planes' blocks is at most %d; at least %lld bytes free after the start\n",
               maxBlocks, maxBytes, maxWork, maxTo, minLeft);
    printf("heap: the least room is in level %d (%d bytes), the fewest free blocks in level %d (%d), the least room "
           "in a played level in level %d (%d bytes)\n",
           *worstBytes, minGap, *worstBlocks, minBlocks, *worstPlayed, minPlayed);
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    const char *fixturePath = argc > 2 ? argv[2] : "tests/fixtures/cube.models.bin";
    static uint8_t fixture[1 << 16];
    size_t fixtureN;
    int worstBytes = 0, worstBlocks = 0, worstPlayed = 0;
    size_t n;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        printf("skipped: no ROM (bankwalk ROM [FIXTURE [FRAMES]])\n");
        return 0;
    }
    fclose(f);
    if (argc > 3)
        frames = (unsigned)atoi(argv[3]);
    f = fopen(fixturePath, "rb");
    if (!f) {
        printf("FAIL no fixture at %s\n", fixturePath);
        return 1;
    }
    fixtureN = fread(fixture, 1, sizeof fixture, f);
    fclose(f);
    engine_default_options(&opt);
    engine = engine_create(argv[1], &opt, err, sizeof err);
    if (!engine) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(engine));
    if (!boot)
        return 1;
    engine_save(engine, boot);
    rom = engine_rom(engine, &romSize);

    run_case("Glovebox's cube", fixture, fixtureN, LEVEL, 2, 1, 0);
    no_slot(fixture, fixtureN);
    builder_cases();

    heap_survey(&worstBytes, &worstBlocks, &worstPlayed);
    planes_cases();
    n = largest();
    /* The scenes read their first actor's animation, and a pack's scenery in front of it stops them. A played level
       must play. */
    run_case("the largest pack, at the level with the least room", out, n, worstBytes, 16, 1,
             worstBytes >= FIRST_SCENE);
    if (worstBlocks != worstBytes)
        run_case("the largest pack, at the level with the fewest free blocks", out, n, worstBlocks, 16, 1,
                 worstBlocks >= FIRST_SCENE);
    if (worstPlayed != worstBytes && worstPlayed != worstBlocks)
        run_case("the largest pack, at the played level with the least room", out, n, worstPlayed, 16, 1, 0);

    printf("%s a pack's own banks load as the bank check and its shadow walk say\n", failed ? "FAIL" : "ok  ");
    free(withBuf);
    free(boot);
    engine_free(engine);
    return failed;
}
