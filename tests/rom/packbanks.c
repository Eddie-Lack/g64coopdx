/* Retail banks that a level pack names, in the real game, with the ROM (docs/discovery/model-banks.md, step 9).
 *
 * A pack's models.bin may name retail texture and object banks by number. The game loads them from the player's own
 * ROM into a table on its heap (G64LoadPackBanks in weemitt's g64packbanks.c). This test runs the game with such
 * packs. The pack's level file is a copy of the ROM's own file of that level, in memory, unless a case says so.
 *
 *   - The cost of each allowed bank: the heap blocks and bytes it takes, and for an object bank the texture lookups
 *     that find nothing when it loads with its world's texture banks and when it loads alone.
 *   - Every allowed object bank, 4 at a time, with its world's texture banks: every group in every level, with 16
 *     players who press buttons at random, for FRAMES frames. The program must not stop, every bank must load, and a
 *     level must stop as unplayable only when it does so without the pack's banks too. The least free heap and blocks
 *     are printed.
 *   - The four object banks that take the most blocks, and the four that take the most bytes, with their texture
 *     banks, and the four largest banks of each kind: in the levels with the least room.
 *   - Every allowed object bank alone, without its texture banks, with all its objects that level 13 lacks as
 *     scenery: the level plays, or stops as unplayable when a sprite's texture is missing. Two runs give the same
 *     hash, and the program never stops.
 *   - The lookup order: the level's own world texture bank named as a retail bank. Every id is then in both, and the
 *     table answers a lookup only with the first flag.
 *   - A level whose file names objects of a retail bank as scenery: FindObject finds them in the table, and the level
 *     plays. Without the retail number the level stops as unplayable, and the client's rule for the level's ids
 *     (g64_pack_check_ids) refuses the file.
 *   - OUTRO and FLYTHRU, whose objects have the ball's and the hand's names: the game's ball and hand texture words
 *     and the bits of the words it has are the same as without them.
 *   - The client's rule for retail object banks (g64_pack_check_retail_sprites): every allowed object bank, alone and
 *     with its world's texture banks, in every level. The rule refuses the pack exactly when the game stops the level
 *     because a sprite's texture is missing. The table of the levels' texture banks (bankranges.h) equals the game's.
 *   - Save, play, load, play again in a level with retail banks: the same memory hash. Also save, start another level
 *     with other retail banks or none, load: the table is back, and the hash equals the run that was never stopped.
 *   - The level ends and the hub loads: the table is gone, no lookup is answered from it after the level's end, and
 *     no lookup reaches it while the level's slots are freed.
 *
 * The output holds no address and no data from the ROM: only bank numbers, counts and hashes. make packbanks compares
 * it between Linux and Wine.
 *
 * Usage: packbanks ROM [FRAMES]. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/bankcheck.h"
#include "../../src/client/bankranges.h"
#include "../../src/client/levcheck.h"
#include "../../src/client/pack.h"
#include "../../src/client/packrom.h"
#include "../../src/client/rombanks.h"
#include "../../weemitt/bruteforce/engine/engine.h"

typedef struct {
    unsigned levels, allGaribs;
    unsigned char hubTarget, hubLevel, worldComplete, raisedBit;
} G64Save;
extern int g64Online;
extern unsigned short g64Buttons[16];
extern signed char g64StickX[16], g64StickY[16];
extern int g64PackHeapBlocks;
extern int g64PackTextureHits, g64PackObjectHits, g64PackTextureMisses, g64PackFreedSlotReads, g64PackSpriteStops;
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
void G64SetStartRecord(int world, const G64Save *s);
void G64SetPack(int level, unsigned rom, unsigned size);
void G64SetPackModels(unsigned texRom, unsigned texSize, unsigned objRom, unsigned objSize);
void G64SetPackRetail(int first, const unsigned *tex, int numTex, const unsigned *obj, int numObj);
void G64PackTableCounts(int *numTex, int *numObj);
int G64PackTableObjectIds(int i, unsigned *ids, int max);
int G64PackTableHasName(const char *name);
int G64PackGameLevel(void);
int G64LevelBankNumbers(int num, unsigned *banks, int max);
int G64LevelTextureBanks(int num, unsigned *banks, int max);
int G64BankRom(int kind, unsigned num, unsigned *start, unsigned *end);
unsigned G64LevelFileRom(int num, unsigned *size);
int G64LevelUnplayable(void);
unsigned G64TexWordsLoaded(void);
int JallocFreeBlocks(void);
void ExitLevel(int type, int pause, int complete);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);
extern unsigned char jallocControl[];
/* The ball's and the hand's texture words (weemitt's g64players.c, tex_word): 32-bit words in the game's memory. */
extern unsigned ballTextureData, ballTexturePalette, ballTextureData2, ballTexturePalette2;
extern unsigned ballBitTextureData, ballBitTexturePalette, handTextureData[2], handTexturePalette[2];

/* enum TexBank and enum Level (weemitt/include/enums.h), and the hub's exit (enum ExitType). */
enum { GENERIC_TEX = 2, FONT_TEX = 10, CAMEO_TEX = 11, FLYTHRU_TEX = 13 };
enum { LEVELS = 48, LEVEL = 13, OUTRO_BANK = 66, FLYTHRU_BANK = 69, HUB_EXIT = 13, T_BACKGROUND = 145,
       T_END = 0x7D00, QUIET = 300, MAXBANKS = 128 };
#define BOT_BUTTONS 0xE01Fu
#define RETAIL 4

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

static int heap_free(void) {
    int size, used, dyn;
    memcpy(&size, jallocControl + 0x6404, 4);
    memcpy(&used, jallocControl + 0x6410, 4);
    memcpy(&dyn, jallocControl + 0x6414, 4);
    return size - used - dyn;
}

/* ---- The pack: a level file, no banks of its own, and retail numbers. */

typedef struct {
    unsigned tex[RETAIL], obj[RETAIL];
    int numTex, numObj, first;
} Retail;

static uint8_t *withBuf;

/* A copy of level's own file from the ROM, or NULL. */
static const uint8_t *own_file(int level, size_t *n) {
    unsigned size = 0, at = G64LevelFileRom(level, &size);
    if (at == 0 || size < 8 || at + size > romSize)
        return NULL;
    *n = size;
    return rom + at;
}

/* Starts level with the pack's level file lev (NULL: the level's own) and the retail banks r (NULL: none), with
   players players, online (each player's input from g64Buttons) or not. */
static void start(int level, const uint8_t *lev, size_t levN, const Retail *r, int players, int online) {
    G64Save s;
    unsigned mask = players >= 16 ? 0xFFFFu : (1u << players) - 1;
    if (!lev)
        lev = own_file(level, &levN);
    free(withBuf);
    withBuf = malloc(romSize + levN);
    if (!withBuf || !lev) {
        printf("FAIL no level file for level %d\n", level);
        exit(1);
    }
    memcpy(withBuf, rom, romSize);
    memcpy(withBuf + romSize, lev, levN);
    engine_load(engine, boot);
    engine_set_rom(engine, withBuf, romSize + levN);
    G64SetPack(level, (unsigned)romSize, (unsigned)levN);
    G64SetPackModels(0, 0, 0, 0);
    if (r)
        G64SetPackRetail(r->first, r->tex, r->numTex, r->obj, r->numObj);
    g64Online = online;
    G64StartPlayers(mask);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
}

static void stop(void) {
    G64SetPack(-1, 0, 0);
    G64SetPackModels(0, 0, 0, 0);
    g64Online = 1;
    engine_load(engine, boot);
    engine_set_rom(engine, rom, romSize);
}

static unsigned seed;

static unsigned next(void) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

/* Players who press buttons at random, as tests/rom/bots.c does. The first QUIET frames they press A every 30
   frames. from is the frame count the run starts at, for a run that goes on from a saved state. */
static unsigned short buttons[16];
static signed char sx[16], sy[16];

static void play(int players, unsigned from, unsigned count, int *minFree, int *minBlocks) {
    unsigned mask = players >= 16 ? 0xFFFFu : (1u << players) - 1;
    unsigned f;
    int p;
    for (f = from; f < from + count; f++) {
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
        if (minFree && heap_free() < *minFree)
            *minFree = heap_free();
        if (minBlocks && JallocFreeBlocks() < *minBlocks)
            *minBlocks = JallocFreeBlocks();
    }
}

static void bots_seed(int level) {
    seed = 12345u + (unsigned)level;
    memset(buttons, 0, sizeof buttons);
    memset(sx, 0, sizeof sx);
    memset(sy, 0, sizeof sy);
}

/* ---- The banks. */

static unsigned texBanks[MAXBANKS], objBanks[MAXBANKS];
static int numTexBanks, numObjBanks;
/* Each object bank's world texture banks, from the levels that load it. */
static unsigned texOf[MAXBANKS][RETAIL];
static int numTexOf[MAXBANKS];
/* What each bank costs: blocks and bytes, and for an object bank its texture misses. */
static int objBlocks[MAXBANKS], objBytes[MAXBANKS], objMisses[MAXBANKS], objMissesAlone[MAXBANKS];
static int texBytes[MAXBANKS];

static void list_banks(void) {
    unsigned n, s, e;
    for (n = 0; n < MAXBANKS; n++) {
        if (G64BankRom(0, n, &s, &e) && n != FONT_TEX && n != GENERIC_TEX)
            texBanks[numTexBanks++] = n;
        if (G64BankRom(1, n, &s, &e) && n > 1)
            objBanks[numObjBanks++] = n;
    }
    printf("allowed: %d texture banks and %d object banks\n", numTexBanks, numObjBanks);
}

static void add_tex(unsigned obj, unsigned tex) {
    int i;
    for (i = 0; i < numTexOf[obj]; i++)
        if (texOf[obj][i] == tex)
            return;
    if (numTexOf[obj] < RETAIL)
        texOf[obj][numTexOf[obj]++] = tex;
}

/* Pairs each object bank with the texture banks of the levels that load it. */
static void pair_banks(void) {
    int level, i, j, nt, no;
    unsigned objs[8], texs[4];
    for (level = 0; level < LEVELS; level++) {
        no = G64LevelBankNumbers(level, objs, 8);
        nt = G64LevelTextureBanks(level, texs, 4);
        for (i = 0; i < no; i++)
            for (j = 0; j < nt; j++)
                if (objs[i] > 1 && objs[i] < MAXBANKS)
                    add_tex(objs[i], texs[j]);
    }
}

/* The free blocks and bytes after level 13 starts, with r or without. */
static void cost(const Retail *r, int *blocks, int *bytes, int *misses) {
    start(LEVEL, NULL, 0, r, 2, 1);
    *blocks = JallocFreeBlocks();
    *bytes = heap_free();
    *misses = g64PackTextureMisses;
    stop();
}

static void costs(void) {
    int baseBlocks, baseBytes, b, y, m, i, k, best;
    Retail r;
    cost(NULL, &baseBlocks, &baseBytes, &m);
    for (i = 0; i < numTexBanks; i++) {
        memset(&r, 0, sizeof r);
        r.tex[0] = texBanks[i];
        r.numTex = 1;
        cost(&r, &b, &y, &m);
        texBytes[texBanks[i]] = baseBytes - y;
        printf("texture bank %u: %d blocks, %d bytes\n", texBanks[i], baseBlocks - b, baseBytes - y);
    }
    for (i = 0; i < numObjBanks; i++) {
        unsigned o = objBanks[i];
        memset(&r, 0, sizeof r);
        r.obj[0] = o;
        r.numObj = 1;
        cost(&r, &b, &y, &m);
        objMissesAlone[o] = m;
        if (numTexOf[o] == 0) {
            /* No level loads it: the texture bank with the fewest misses. */
            best = -1;
            for (k = 0; k < numTexBanks; k++) {
                Retail t = r;
                int b2, y2, m2;
                t.tex[0] = texBanks[k];
                t.numTex = 1;
                cost(&t, &b2, &y2, &m2);
                if (best < 0 || m2 < best) {
                    best = m2;
                    numTexOf[o] = 1;
                    texOf[o][0] = texBanks[k];
                }
            }
        }
        memcpy(r.tex, texOf[o], sizeof r.tex);
        r.numTex = numTexOf[o];
        {
            int tb, ty, tm;
            Retail t;
            memset(&t, 0, sizeof t);
            memcpy(t.tex, texOf[o], sizeof t.tex);
            t.numTex = numTexOf[o];
            cost(&t, &tb, &ty, &tm);
            cost(&r, &b, &y, &m);
            objBlocks[o] = tb - b;
            objBytes[o] = ty - y;
        }
        objMisses[o] = m;
        printf("object bank %u with texture banks", o);
        for (k = 0; k < numTexOf[o]; k++)
            printf(" %u", texOf[o][k]);
        printf(": %d blocks, %d bytes, %d textures not found (%d without its texture banks)\n", objBlocks[o],
               objBytes[o], objMisses[o], objMissesAlone[o]);
    }
}

/* Adds object bank o and its texture banks to r. Returns 0 when they don't fit in RETAIL of each kind. */
static int add_group(Retail *r, unsigned o) {
    Retail t = *r;
    int k, j, have;
    if (t.numObj >= RETAIL)
        return 0;
    t.obj[t.numObj++] = o;
    for (k = 0; k < numTexOf[o]; k++) {
        for (have = 0, j = 0; j < t.numTex; j++)
            have |= t.tex[j] == texOf[o][k];
        if (!have) {
            if (t.numTex >= RETAIL)
                return 0;
            t.tex[t.numTex++] = texOf[o][k];
        }
    }
    *r = t;
    return 1;
}

static void print_retail(const Retail *r) {
    int i;
    printf("texture banks");
    for (i = 0; i < r->numTex; i++)
        printf(" %u", r->tex[i]);
    printf(", object banks");
    for (i = 0; i < r->numObj; i++)
        printf(" %u", r->obj[i]);
}

/* 1 when level stops as unplayable without the pack's banks, with 16 players. */
static int stops_alone(int level) {
    int stopped;
    start(level, NULL, 0, NULL, 16, 1);
    bots_seed(level);
    play(16, 0, frames, NULL, NULL);
    stopped = G64LevelUnplayable();
    stop();
    return stopped;
}

static int leastBlocks = 1 << 30, leastBytes = 1 << 30;

/* Plays level with r and 16 players. Returns 1 when it went as it must. */
static int run_retail(int level, const Retail *r, int alone) {
    int minFree = 1 << 30, minBlocks = 1 << 30, nt, no, ok;
    start(level, NULL, 0, r, 16, 1);
    G64PackTableCounts(&nt, &no);
    ok = nt == r->numTex && no == r->numObj;
    bots_seed(level);
    play(16, 0, frames, &minFree, &minBlocks);
    ok &= G64LevelUnplayable() == alone;
    printf("%s level %2d with ", ok ? "ok  " : "FAIL", level);
    print_retail(r);
    printf(": %d blocks free at the load, at least %d blocks and %d bytes free, hash %08x%s\n", g64PackHeapBlocks,
           minBlocks, minFree, port_hash_ram(), alone ? ", unplayable as without them" : "");
    if (minBlocks < leastBlocks)
        leastBlocks = minBlocks;
    if (minFree < leastBytes)
        leastBytes = minFree;
    stop();
    failed |= !ok;
    return ok;
}

static Retail groups[MAXBANKS];
static int numGroups;

static void every_level(void) {
    int i, level;
    Retail g;
    memset(&g, 0, sizeof g);
    for (i = 0; i < numObjBanks; i++) {
        if (!add_group(&g, objBanks[i])) {
            groups[numGroups++] = g;
            memset(&g, 0, sizeof g);
            add_group(&g, objBanks[i]);
        }
    }
    if (g.numObj)
        groups[numGroups++] = g;
    printf("%d groups of object banks\n", numGroups);
    for (level = 0; level < LEVELS; level++) {
        int alone = stops_alone(level);
        for (i = 0; i < numGroups; i++)
            run_retail(level, &groups[i], alone);
    }
    printf("every level: at least %d blocks and %d bytes free\n", leastBlocks, leastBytes);
}

/* The four object banks with the most of a cost, and their texture banks, as far as they fit. */
static void worst(const int *by, Retail *r) {
    int used[MAXBANKS] = {0}, i, k, pick;
    memset(r, 0, sizeof *r);
    for (k = 0; k < numObjBanks && r->numObj < RETAIL; k++) {
        pick = -1;
        for (i = 0; i < numObjBanks; i++)
            if (!used[objBanks[i]] && (pick < 0 || by[objBanks[i]] > by[pick]))
                pick = (int)objBanks[i];
        if (pick < 0)
            break;
        used[pick] = 1;
        add_group(r, (unsigned)pick);
    }
}

/* The four largest texture banks and the four largest object banks, each kind on its own. */
static void largest(Retail *r) {
    int used[MAXBANKS] = {0}, k, i, pick;
    memset(r, 0, sizeof *r);
    for (k = 0; k < RETAIL; k++) {
        pick = -1;
        for (i = 0; i < numTexBanks; i++)
            if (!used[texBanks[i]] && (pick < 0 || texBytes[texBanks[i]] > texBytes[pick]))
                pick = (int)texBanks[i];
        if (pick >= 0) {
            used[pick] = 1;
            r->tex[r->numTex++] = (unsigned)pick;
        }
    }
    memset(used, 0, sizeof used);
    for (k = 0; k < RETAIL; k++) {
        pick = -1;
        for (i = 0; i < numObjBanks; i++)
            if (!used[objBanks[i]] && (pick < 0 || objBytes[objBanks[i]] > objBytes[pick]))
                pick = (int)objBanks[i];
        if (pick >= 0) {
            used[pick] = 1;
            r->obj[r->numObj++] = (unsigned)pick;
        }
    }
}

static void worst_cases(void) {
    static const int levels[] = {15, 35, 7, 46};
    Retail byBlocks, byBytes, most;
    unsigned i;
    worst(objBlocks, &byBlocks);
    worst(objBytes, &byBytes);
    largest(&most);
    leastBlocks = leastBytes = 1 << 30;
    for (i = 0; i < sizeof levels / sizeof levels[0]; i++) {
        int alone = stops_alone(levels[i]);
        run_retail(levels[i], &byBlocks, alone);
        run_retail(levels[i], &byBytes, alone);
        run_retail(levels[i], &most, alone);
    }
    printf("the costliest banks: at least %d blocks and %d bytes free\n", leastBlocks, leastBytes);
}

/* ---- A level file that names retail objects. */

static size_t endAt;
static int sawEnd;

static void find_end(void *user, size_t offset, int token) {
    (void)user;
    if (token == T_END) {
        endAt = offset;
        sawEnd = 1;
    }
}

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void putf(uint8_t *p, float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    put32(p, v);
}

/* level's own file with a BACKGROUND token for each id, before the end token. */
static uint8_t *with_scenery(int level, const uint32_t *ids, unsigned count, size_t *n) {
    size_t size = 0, at2 = 0, grown;
    const uint8_t *own = own_file(level, &size);
    uint8_t *lev;
    unsigned i;
    sawEnd = 0;
    if (!own || g64_lev_check(own, size, find_end, NULL, &at2) != NULL || !sawEnd)
        return NULL;
    grown = size + (size_t)count * 26;
    lev = calloc(1, grown);
    if (!lev)
        return NULL;
    memcpy(lev, own, endAt);
    for (i = 0; i < count; i++) {
        uint8_t *t = lev + endAt + 26 * i;
        put16(t, T_BACKGROUND);
        put32(t + 2, ids[i]);
        memcpy(t + 6, "pack", 4);
        putf(t + 14, 40.0f * (float)(i % 8));
        putf(t + 18, 0.0f);
        putf(t + 22, 40.0f * (float)(i / 8));
    }
    memcpy(lev + endAt + 26 * count, own + endAt, size - endAt);
    put32(lev, (uint32_t)grown);
    if (g64_lev_check(lev, grown, NULL, NULL, &at2) != NULL) {
        free(lev);
        return NULL;
    }
    *n = grown;
    return lev;
}

/* 1 when object bank o is one that level loads itself. */
static int level_has_bank(int level, unsigned o) {
    unsigned objs[8];
    int i, n = G64LevelBankNumbers(level, objs, 8);
    for (i = 0; i < n; i++)
        if (objs[i] == o)
            return 1;
    return 0;
}

/* The directory ids of object bank o, loaded alone into level 13's table, that no bank of level 13 has. */
static unsigned scenery_ids(unsigned o, const G64RomBanks *index, uint32_t *ids, unsigned max) {
    Retail r;
    static unsigned all[512];
    unsigned n = 0, k, levelObjs[8];
    int got, nb, b, mine;
    memset(&r, 0, sizeof r);
    r.obj[0] = o;
    r.numObj = 1;
    start(LEVEL, NULL, 0, &r, 2, 1);
    got = G64PackTableObjectIds(0, all, 512);
    stop();
    nb = G64LevelBankNumbers(LEVEL, levelObjs, 8);
    for (k = 0; got > 0 && k < (unsigned)got && k < 512 && n < max; k++) {
        for (mine = 0, b = 0; b < nb; b++)
            mine |= g64_rombanks_bank_has((void *)index, levelObjs[b], all[k]);
        if (!mine)
            ids[n++] = all[k];
    }
    return n;
}

/* Plays level 13 with the objects of bank o as scenery, with r, twice. With mustFind 1, the table must answer each
   object's lookup, and the controls run. With 2, the level must play. With 0, it may stop as unplayable. */
static void scenery_case(const char *what, unsigned o, const Retail *r, int mustFind, const G64RomBanks *index) {
    /* Every object of the bank that level 13 lacks. An object bank holds at most a few hundred. */
    static uint32_t ids[512];
    unsigned count = scenery_ids(o, index, ids, 512);
    size_t n = 0, at = 0;
    uint8_t *lev = count ? with_scenery(LEVEL, ids, count, &n) : NULL;
    int found, stopped;
    unsigned h1, h2;
    char line[240];
    if (!count) {
        printf("ok   %s: object bank %u has no object that level %d lacks\n", what, o, LEVEL);
        return;
    }
    if (!lev) {
        printf("FAIL %s: no level file with object bank %u's objects\n", what, o);
        failed = 1;
        return;
    }
    start(LEVEL, lev, n, r, 16, 1);
    found = g64PackObjectHits;
    stopped = G64LevelUnplayable();
    bots_seed(LEVEL);
    play(16, 0, frames, NULL, NULL);
    stopped |= G64LevelUnplayable();
    h1 = port_hash_ram();
    start(LEVEL, lev, n, r, 16, 1);
    bots_seed(LEVEL);
    play(16, 0, frames, NULL, NULL);
    h2 = port_hash_ram();
    stop();
    snprintf(line, sizeof line, "%s: %u of object bank %u's objects as scenery, %d found in the table, %d frames, "
             "hash %08x", what, count, o, found, frames, h1);
    if (mustFind == 1) {
        G64Models info;
        g64_level_banks banks;
        unsigned nums[G64_LEVEL_BANKS_MAX];
        memset(&info, 0, sizeof info);
        info.numObj = 1;
        info.obj[0] = (uint8_t)o;
        banks.count = (unsigned)G64LevelBankNumbers(LEVEL, nums, G64_LEVEL_BANKS_MAX);
        memcpy(banks.num, nums, sizeof nums);
        check(found >= (int)count && !stopped && h1 == h2, line);
        check(g64_pack_check_ids(lev, n, (const uint8_t *)"", &info, &banks, g64_rombanks_bank_has, (void *)index,
                                 &at) == NULL,
              "the client's rule for the level's ids accepts the file with the retail number");
        check(g64_pack_check_ids(lev, n, NULL, NULL, &banks, g64_rombanks_bank_has, (void *)index, &at) != NULL,
              "the client's rule for the level's ids refuses the file without it");
        /* The control: the same file without the retail bank. */
        start(LEVEL, lev, n, NULL, 2, 1);
        check(G64LevelUnplayable(), "without the retail number the level stops as unplayable");
        stop();
    } else {
        /* Without its texture banks, a bank whose sprites lack their textures stops the level. */
        snprintf(line + strlen(line), sizeof line - strlen(line), ", %s", stopped ? "unplayable" : "played");
        check(h1 == h2 && (mustFind != 2 || !stopped), line);
    }
    free(lev);
}

/* Every allowed object bank alone, without its texture banks. */
static void textures_missing(const G64RomBanks *index) {
    Retail r;
    int i;
    char what[80];
    for (i = 0; i < numObjBanks; i++) {
        memset(&r, 0, sizeof r);
        r.obj[0] = objBanks[i];
        r.numObj = 1;
        snprintf(what, sizeof what, "object bank %u without its texture banks", objBanks[i]);
        scenery_case(what, objBanks[i], &r, 0, index);
    }
}

/* ---- The lookup order. */

static void lookup_order(void) {
    unsigned tex[4], objs[8];
    int nt = G64LevelTextureBanks(LEVEL, tex, 4), i, k, nb;
    unsigned other = 0;
    int hits[2];
    Retail r;
    /* An object bank of a level of the same world that level 13 doesn't load. */
    for (k = 0; k < LEVELS && !other; k++) {
        unsigned t2[4];
        if (G64LevelTextureBanks(k, t2, 4) < 1 || t2[0] != tex[0])
            continue;
        nb = G64LevelBankNumbers(k, objs, 8);
        for (i = 0; i < nb && !other; i++)
            if (objs[i] > 1 && !level_has_bank(LEVEL, objs[i]))
                other = objs[i];
    }
    if (nt < 1 || !other) {
        printf("FAIL no bank for the test of the lookup order\n");
        failed = 1;
        return;
    }
    for (k = 0; k < 2; k++) {
        memset(&r, 0, sizeof r);
        r.tex[0] = tex[0];
        r.numTex = 1;
        r.obj[0] = other;
        r.numObj = 1;
        r.first = k;
        start(LEVEL, NULL, 0, &r, 2, 1);
        hits[k] = g64PackTextureHits;
        stop();
    }
    printf("%s texture bank %u, the level's own, and object bank %u as retail banks: the table answers %d lookups "
           "without the first flag and %d with it\n", hits[0] == 0 && hits[1] > 0 ? "ok  " : "FAIL", tex[0], other,
           hits[0], hits[1]);
    failed |= !(hits[0] == 0 && hits[1] > 0);
}

/* ---- The ball's and the hand's names. */

static void words(unsigned *w) {
    w[0] = ballTextureData;
    w[1] = ballTexturePalette;
    w[2] = ballTextureData2;
    w[3] = ballTexturePalette2;
    w[4] = ballBitTextureData;
    w[5] = ballBitTexturePalette;
    w[6] = handTextureData[0];
    w[7] = handTextureData[1];
    w[8] = handTexturePalette[0];
    w[9] = handTexturePalette[1];
    w[10] = G64TexWordsLoaded();
}

static void reserved_names(void) {
    static const int levels[] = {LEVEL, 0, 1};
    unsigned before[11], after[11], i;
    Retail r;
    memset(&r, 0, sizeof r);
    r.tex[0] = CAMEO_TEX;
    r.tex[1] = FLYTHRU_TEX;
    r.numTex = 2;
    r.obj[0] = OUTRO_BANK;
    r.obj[1] = FLYTHRU_BANK;
    r.numObj = 2;
    for (i = 0; i < sizeof levels / sizeof levels[0]; i++) {
        char line[160];
        int names;
        start(levels[i], NULL, 0, NULL, 2, 1);
        words(before);
        stop();
        start(levels[i], NULL, 0, &r, 2, 1);
        words(after);
        names = G64PackTableHasName("GShinL") && G64PackTableHasName("GShinR") && G64PackTableHasName("gball");
        stop();
        snprintf(line, sizeof line, "level %d with OUTRO and FLYTHRU: the table has GShinL, GShinR and gball (%d), and "
                 "the ball and hand words are unchanged (bits %03x)", levels[i], names, before[10]);
        check(names && memcmp(before, after, sizeof before) == 0, line);
    }
}

/* ---- The client's rule for retail object banks. */

/* What the game does at level start: 1 when a retail object bank stopped the level for a missing sprite texture. */
static int game_stops(int level, const Retail *r) {
    int stops;
    start(level, NULL, 0, r, 2, 1);
    stops = g64PackSpriteStops > 0;
    stop();
    return stops;
}

static int client_refuses(int level, const Retail *r, const G64RomBanks *index) {
    G64Models info;
    unsigned bank;
    int i;
    memset(&info, 0, sizeof info);
    info.numTex = (uint8_t)r->numTex;
    info.numObj = (uint8_t)r->numObj;
    for (i = 0; i < r->numTex; i++)
        info.tex[i] = (uint8_t)r->tex[i];
    for (i = 0; i < r->numObj; i++)
        info.obj[i] = (uint8_t)r->obj[i];
    return g64_pack_check_retail_sprites(level, &info, index, &bank) != NULL;
}

static void client_rule(const G64RomBanks *index) {
    int level, i, k, same = 1, differ = 0, refused[2] = {0, 0}, cases = 0;
    for (level = 0; level < LEVELS; level++) {
        unsigned game[4], table[4];
        int n = G64LevelTextureBanks(level, game, 4), m = g64_level_texture_banks(level, table, 4);
        same &= n >= 1 && m == n + 2 && table[0] == FONT_TEX && table[1] == GENERIC_TEX &&
                memcmp(game, table + 2, (size_t)n * sizeof game[0]) == 0;
    }
    check(same, "the table of the levels' texture banks in bankranges.h equals G64LevelTextureBanks");
    for (i = 0; i < numObjBanks; i++) {
        for (k = 0; k < 2; k++) {
            Retail r;
            memset(&r, 0, sizeof r);
            if (k)
                add_group(&r, objBanks[i]);
            else {
                r.obj[0] = objBanks[i];
                r.numObj = 1;
            }
            for (level = 0; level < LEVELS; level++) {
                int g = game_stops(level, &r), c = client_refuses(level, &r, index);
                cases++;
                refused[k] += c;
                if (g != c) {
                    differ++;
                    printf("FAIL level %d with ", level);
                    print_retail(&r);
                    printf(": the game %s, the client %s\n", g ? "stops" : "plays", c ? "refuses" : "accepts");
                }
            }
        }
    }
    printf("%s the client's rule for retail object banks agrees with the game in %d of %d cases: it refuses %d "
           "alone and %d with their texture banks\n", differ ? "FAIL" : "ok  ", cases - differ, cases, refused[0],
           refused[1]);
    failed |= differ != 0;
}

/* ---- Save and load. */

/* The table's counts and the ids of its first object bank, as one number. */
static unsigned table_sum(int *nt, int *no) {
    static unsigned ids[512];
    unsigned sum = 0;
    int n, k;
    G64PackTableCounts(nt, no);
    n = G64PackTableObjectIds(0, ids, 512);
    for (k = 0; k < n && k < 512; k++)
        sum = sum * 31u + ids[k];
    return sum + (unsigned)n;
}

/* With other 0, the load comes right after the save. Otherwise level other starts between them, with detour as its
   retail banks (NULL: none), and the ROM buffer of the saved level is put back before the load. */
static void save_load(const Retail *r, int level, int other, const Retail *detour) {
    void *mid = malloc(engine_state_size(engine));
    unsigned h1, h2, seedMid, sum1, sum2;
    unsigned short bMid[16];
    signed char xMid[16], yMid[16];
    uint8_t *keep = NULL;
    size_t keepN = 0, levN = 0;
    int nt1, no1, nt2, no2, ntD = -2, noD = -2;
    if (!mid)
        exit(1);
    start(level, NULL, 0, r, 16, 1);
    bots_seed(level);
    play(16, 0, frames / 2, NULL, NULL);
    engine_save(engine, mid);
    seedMid = seed;
    memcpy(bMid, buttons, sizeof bMid);
    memcpy(xMid, sx, sizeof xMid);
    memcpy(yMid, sy, sizeof yMid);
    sum1 = table_sum(&nt1, &no1);
    play(16, frames / 2, frames / 2, NULL, NULL);
    h1 = port_hash_ram();
    if (other) {
        if (!own_file(level, &levN))
            exit(1);
        keepN = romSize + levN;
        keep = malloc(keepN);
        if (!keep)
            exit(1);
        memcpy(keep, withBuf, keepN);
        start(other, NULL, 0, detour, 2, 1);
        play(2, 0, 30, NULL, NULL);
        G64PackTableCounts(&ntD, &noD);
        engine_set_rom(engine, keep, keepN);
    }
    engine_load(engine, mid);
    sum2 = table_sum(&nt2, &no2);
    seed = seedMid;
    memcpy(buttons, bMid, sizeof bMid);
    memcpy(sx, xMid, sizeof xMid);
    memcpy(sy, yMid, sizeof yMid);
    play(16, frames / 2, frames / 2, NULL, NULL);
    h2 = port_hash_ram();
    stop();
    free(keep);
    free(mid);
    {
        int ok = h1 == h2 && nt1 == r->numTex && no1 == r->numObj && nt2 == nt1 && no2 == no1 && sum1 == sum2;
        printf("%s save at frame %u and load: hash %08x and %08x, table %d %d and %d %d, ", ok ? "ok  " : "FAIL",
               frames / 2, h1, h2, nt1, no1, nt2, no2);
        print_retail(r);
        printf(" in level %d", level);
        if (other)
            printf(", level %d between them with table %d %d", other, ntD, noD);
        printf("\n");
        failed |= !ok;
    }
}

/* ---- The level ends. */

/* With complete 1 the level ends as completed. Both ways free the slots in FreeLevel2 (game.c). */
static void level_ends(const Retail *r, int level, int complete) {
    int f, hub = -1, nt, no, hits;
    start(level, NULL, 0, r, 1, 0);
    G64PackTableCounts(&nt, &no);
    check(nt == r->numTex && no == r->numObj && g64PackTextureHits > 0, "the retail banks load in a game offline");
    bots_seed(level);
    play(1, 0, QUIET, NULL, NULL);
    ExitLevel(HUB_EXIT, 1, complete);
    /* Lookups from here on: none may be answered from the table. */
    g64PackTextureHits = g64PackObjectHits = g64PackFreedSlotReads = 0;
    for (f = 0; f < 6000 && hub < 0; f++) {
        play(1, 0, 1, NULL, NULL);
        if (G64PackGameLevel() != level)
            hub = G64PackGameLevel();
    }
    play(1, QUIET, frames, NULL, NULL);
    G64PackTableCounts(&nt, &no);
    hits = g64PackTextureHits + g64PackObjectHits;
    {
        char line[240];
        snprintf(line, sizeof line, "level %d ends%s and level %d loads: no table (%d %d), no lookup answered from it "
                 "(%d), none reached it while the slots were freed (%d), hash %08x", level, complete ? " won" : "",
                 hub, nt, no, hits, g64PackFreedSlotReads, port_hash_ram());
        check(hub >= 0 && hub != level && nt == -1 && no == -1 && hits == 0 && g64PackFreedSlotReads == 0 &&
                  !G64LevelUnplayable(),
              line);
    }
    stop();
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    G64RomBanks *index;
    unsigned scenery = 0;
    Retail r;
    int i;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        printf("skipped: no ROM (packbanks ROM [FRAMES])\n");
        return 0;
    }
    fclose(f);
    if (argc > 2)
        frames = (unsigned)atoi(argv[2]);
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
    index = g64_rombanks_build_game(rom, romSize);
    if (!index) {
        printf("FAIL no index of the ROM's banks\n");
        return 1;
    }

    list_banks();
    pair_banks();
    costs();
    every_level();
    worst_cases();

    /* A retail object bank that level 13 doesn't load, with misses only without its texture banks. */
    for (i = 0; i < numObjBanks && !scenery; i++)
        if (!level_has_bank(LEVEL, objBanks[i]) && objMisses[objBanks[i]] == 0 && objMissesAlone[objBanks[i]] > 0)
            scenery = objBanks[i];
    if (!scenery) {
        printf("FAIL no object bank for the scenery cases\n");
        failed = 1;
    } else {
        memset(&r, 0, sizeof r);
        add_group(&r, scenery);
        scenery_case("a level file that names retail objects", scenery, &r, 1, index);
        memset(&r, 0, sizeof r);
        add_group(&r, scenery);
        r.first = 1;
        scenery_case("retail objects, with the retail textures first", scenery, &r, 2, index);
    }
    textures_missing(index);
    lookup_order();
    reserved_names();
    client_rule(index);
    save_load(&groups[0], 1, 0, NULL);
    save_load(&groups[numGroups - 1], LEVEL, 0, NULL);
    /* Another level between the save and the load: one with no retail banks, and one with other banks, which makes
       another table. */
    save_load(&groups[0], 1, LEVEL, NULL);
    save_load(&groups[0], 1, LEVEL, &groups[numGroups - 1]);
    /* With the first flag, a lookup between the level's end and the new heap would reach the table first. */
    r = groups[0];
    r.first = r.numTex > 0;
    level_ends(&r, 1, 0);
    level_ends(&r, 1, 1);

    printf("%s retail banks load into the table and play\n", failed ? "FAIL" : "ok  ");
    g64_rombanks_free(index);
    free(withBuf);
    free(boot);
    engine_free(engine);
    return failed;
}
