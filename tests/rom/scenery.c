/* Many scenery objects in one level file, in the real game, with the ROM (docs/discovery/level-file-safety.md).
 *
 * A level file may name objects many times as scenery (BACKGROUND tokens). This test copies a level's own file and
 * adds BACKGROUND tokens before its end token, in rows of 16 around the place where the first player starts. Two
 * players press buttons at random for FRAMES frames. The level must play, or stop as unplayable, and the program must
 * never stop. Two runs must give the same memory hash.
 *
 *   - The note's case (model-banks.md): the objects of object bank 29 in the order of its directory, 64 to 200 of
 *     them, in level 35, whose own bank it is. They stand in rows of 8 from x 0 and z 0, as in make packbanks. The
 *     level check must accept each of these files. Before the guards listed below, 104 of them crashed the game in
 *     CheckCollisions.
 *   - The same level with 128 of them and 16 players.
 *   - In 9 levels: the objects of the level's banks that play alone, in turn, and the one that asks for the most
 *     vertices again and again. The count goes up from 50 until the parser stops the level as too large.
 *   - Every level with one object of an id no bank has: the parse stops after the landscape, and the level must stop.
 *
 * These found three faults, now guarded in the game: the frame's buffer of vertices for meshes without a display list
 * (G64VertexRoom in object.c), a sprite without its texture (G64_USABLE in parse.c, AddObjectsSpritesToSpriteList in
 * actor.c), and the restart gates' shadows in a level whose parse stopped (InitMorePersonal in game.c). Each case
 * prints the most vertices a frame asked for, the draws that found no room, and the most commands in a frame's
 * graphics list. They are counted from the start of the run, after the state at power-on is loaded.
 *
 * With "sweep" as the third argument, each case runs in a child process on Linux, and a crash is printed as one.
 * make scenery does not use it.
 *
 * The output holds no address and no data from the ROM: only level numbers, counts and hashes. make scenery compares
 * it between Linux and Wine.
 *
 * Usage: scenery ROM [FRAMES [sweep [LEVEL COUNT]]]. Without a ROM file it prints that it was skipped and exits
 * with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "../../src/client/levcheck.h"
#include "../../src/client/rombanks.h"
#include "../../weemitt/bruteforce/engine/engine.h"

typedef struct {
    unsigned levels, allGaribs;
    unsigned char hubTarget, hubLevel, worldComplete, raisedBit;
} G64Save;
extern int g64Online;
extern unsigned short g64Buttons[16];
extern signed char g64StickX[16], g64StickY[16];
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
void G64SetStartRecord(int world, const G64Save *s);
void G64SetPack(int level, unsigned rom, unsigned size);
void G64SetPackModels(unsigned texRom, unsigned texSize, unsigned objRom, unsigned objSize);
int G64LevelBankNumbers(int num, unsigned *banks, int max);
unsigned G64LevelFileRom(int num, unsigned *size);
int G64LevelUnplayable(void);
int G64ObjectSlotIds(int slot, unsigned *ids, int max);
int G64PlayerPosition(int player, float *x, float *y, float *z);
extern int g64VertexPeak, g64VertexDrops, g64GfxPeak;
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { LEVELS = 48, T_BACKGROUND = 145, T_END = 0x7D00, MAXIDS = 1024 };
#define BOT_BUTTONS 0xE01Fu

static Engine *engine;
static void *boot;
static const uint8_t *rom;
static unsigned long romSize;
static unsigned frames = 300;
/* The players in the level: a bit for each. */
static unsigned present = 3;
static int failed;
static uint8_t *withBuf;
static G64RomBanks *banksIndex;

static const uint8_t *own_file(int level, size_t *n) {
    unsigned size = 0, at = G64LevelFileRom(level, &size);
    if (at == 0 || size < 8 || at + size > romSize)
        return NULL;
    *n = size;
    return rom + at;
}

static void start(int level, const uint8_t *lev, size_t levN) {
    G64Save s;
    free(withBuf);
    withBuf = malloc(romSize + levN);
    if (!withBuf) {
        printf("FAIL out of memory\n");
        exit(1);
    }
    memcpy(withBuf, rom, romSize);
    memcpy(withBuf + romSize, lev, levN);
    engine_load(engine, boot);
    engine_set_rom(engine, withBuf, romSize + levN);
    G64SetPack(level, (unsigned)romSize, (unsigned)levN);
    G64SetPackModels(0, 0, 0, 0);
    g64Online = 1;
    G64StartPlayers(present);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
}

static void stop(void) {
    G64SetPack(-1, 0, 0);
    engine_load(engine, boot);
    engine_set_rom(engine, rom, romSize);
}

static unsigned seed;

static unsigned next(void) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

static void play(int level, unsigned count) {
    unsigned short buttons[16] = {0};
    signed char sx[16] = {0}, sy[16] = {0};
    unsigned f;
    int p;
    seed = 777u + (unsigned)level;
    for (f = 0; f < count; f++) {
        EngineInput in;
        G64SetPresent(present);
        for (p = 0; p < 16; p++) {
            if (!(present >> p & 1))
                continue;
            if (f < 100) {
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
    }
}

static size_t endAt;
static int sawEnd;

static void find_end(void *user, size_t offset, int token) {
    (void)user;
    if (token == T_END) {
        endAt = offset;
        sawEnd = 1;
    }
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

/* The ids of the objects in the level's slots from slot first on, in the order of the slots and of each bank's
   directory, at most max. Also the place where the first player starts. */
static unsigned level_ids(int level, int first, uint32_t *ids, unsigned max, float *at) {
    size_t size = 0;
    const uint8_t *own = own_file(level, &size);
    unsigned n = 0;
    int slot, got;
    if (!own)
        return 0;
    start(level, own, size);
    play(level, 2);
    if (!G64PlayerPosition(0, &at[0], &at[1], &at[2]))
        at[0] = at[1] = at[2] = 0.0f;
    for (slot = first; slot < 5 && n < max; slot++) {
        got = G64ObjectSlotIds(slot, ids + n, (int)(max - n));
        if (got > 0)
            n += (unsigned)got < max - n ? (unsigned)got : max - n;
    }
    stop();
    return n;
}

/* level's own file with count BACKGROUND tokens before its end token. Token i names ids[i % numIds]. They stand in
   rows of 16, 40 units apart, with at in the middle. With at NULL they stand in rows of 8 from x 0 and z 0, at y 0. */
static uint8_t *with_scenery(int level, const uint32_t *ids, unsigned numIds, unsigned count, const float *at,
                             size_t *n) {
    size_t size = 0, at2 = 0, grown;
    const uint8_t *own = own_file(level, &size);
    uint8_t *lev;
    unsigned i, cols = count < 16 ? count : 16, rows = (count + 15) / 16;
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
        t[0] = 0;
        t[1] = T_BACKGROUND;
        put32(t + 2, ids[i % numIds]);
        memcpy(t + 6, "many", 4);
        if (at == NULL) {
            putf(t + 14, 40.0f * (float)(i % 8));
            putf(t + 18, 0.0f);
            putf(t + 22, 40.0f * (float)(i / 8));
        } else {
            putf(t + 14, at[0] + 40.0f * ((float)(i % 16) - (float)(cols - 1) / 2.0f));
            putf(t + 18, at[1]);
            putf(t + 22, at[2] + 40.0f * ((float)(i / 16) - (float)(rows - 1) / 2.0f));
        }
    }
    memcpy(lev + endAt + 26 * count, own + endAt, size - endAt);
    put32(lev, (uint32_t)grown);
    *n = grown;
    return lev;
}

/* Plays one case twice and prints it. The level counts as stopped when it is unplayable right after its start or
   at the end: online, the players of a stopped level go to the hub, which is a level that plays. Returns 1 when it
   stopped. */
static int run_case(const char *what, int level, const uint32_t *ids, unsigned numIds, unsigned count,
                    const float *at) {
    size_t n = 0, at2 = 0;
    uint8_t *lev = numIds ? with_scenery(level, ids, numIds, count, at, &n) : NULL;
    unsigned h1, h2;
    int stopped, peak, drops, gfx;
    if (!lev) {
        printf("FAIL %s, level %d: no level file\n", what, level);
        failed = 1;
        return 0;
    }
    /* The note's files are ones the client's level check lets through. */
    if (at == NULL && g64_lev_check(lev, n, NULL, NULL, &at2) != NULL) {
        printf("FAIL %s, level %d, %u objects: the level check refuses the file\n", what, level, count);
        failed = 1;
    }
    g64VertexPeak = g64VertexDrops = g64GfxPeak = 0;
    start(level, lev, n);
    stopped = G64LevelUnplayable();
    play(level, frames);
    stopped |= G64LevelUnplayable();
    peak = g64VertexPeak;
    drops = g64VertexDrops;
    gfx = g64GfxPeak;
    h1 = port_hash_ram();
    start(level, lev, n);
    play(level, frames);
    h2 = port_hash_ram();
    stop();
    printf("%s %s, level %d, %u objects of %u ids: %s, most vertices %d, %d draws without room, most commands %d, "
           "hash %08x\n", h1 == h2 ? "ok  " : "FAIL", what, level, count, numIds, stopped ? "unplayable" : "played",
           peak, drops, gfx, h1);
    fflush(stdout);
    failed |= h1 != h2;
    free(lev);
    return stopped;
}

/* A case in a child process on Linux in sweep mode: a crash is printed as one, and the sweep goes on. */
static int one(const char *what, int level, const uint32_t *ids, unsigned numIds, unsigned count, const float *at,
               int sweep) {
#ifndef _WIN32
    if (sweep) {
        int status = 0;
        pid_t pid;
        fflush(stdout);
        pid = fork();
        if (pid == 0) {
            _exit(run_case(what, level, ids, numIds, count, at) ? 3 : 0);
        }
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0 && WEXITSTATUS(status) != 3)) {
            printf("FAIL %s, level %d, %u objects: the program stopped (status %d)\n", what, level, count, status);
            failed = 1;
            return 1;
        }
        return WEXITSTATUS(status) == 3;
    }
#endif
    (void)sweep;
    return run_case(what, level, ids, numIds, count, at);
}

/* Keeps the ids whose object, alone as scenery near the start, lets the level play: an object bank may hold objects
   whose sprite textures are in another level's texture bank, and the level stops with those. Returns how many it
   kept, and in best the one that asked for the most vertices. */
static int measure(int level, const uint32_t *id, unsigned count, const float *at, int *peak) {
    size_t n = 0;
    uint8_t *lev = with_scenery(level, id, 1, count, at, &n);
    int stopped;
    if (!lev)
        return 1;
    start(level, lev, n);
    stopped = G64LevelUnplayable();
    play(level, 20);
    *peak = g64VertexPeak;
    stop();
    free(lev);
    return stopped;
}

static unsigned usable_ids(int level, uint32_t *ids, unsigned numIds, const float *at, unsigned *best) {
    unsigned i, kept = 0;
    int most = -1, peak;
    *best = 0;
    for (i = 0; i < numIds; i++) {
        if (measure(level, ids + i, 1, at, &peak))
            continue;
        /* A row of 16 copies: one alone at the start can be too near the camera to be drawn. */
        if (!measure(level, ids + i, 16, at, &peak) && peak > most) {
            most = peak;
            *best = kept;
        }
        ids[kept++] = ids[i];
    }
    return kept;
}

/* Counts from 50 up, until the level stops as unplayable or most is reached: the level's usable objects in turn, and
   the one that asks for the most vertices again and again. */
static void sweep_level(int level, unsigned most, int sweep) {
    static uint32_t ids[MAXIDS];
    float at[3];
    unsigned numIds = level_ids(level, 1, ids, MAXIDS, at), count, best = 0;
    numIds = usable_ids(level, ids, numIds, at, &best);
    printf("     level %d: %u objects of its banks play alone as scenery\n", level, numIds);
    for (count = 50; count <= most; count += count < 200 ? 50 : 100)
        if (one("the level's objects in turn", level, ids, numIds, count, at, sweep))
            break;
    for (count = 50; count <= most; count += count < 200 ? 50 : 100)
        if (one("one object again and again", level, ids + best, numIds ? 1 : 0, count, at, sweep))
            break;
}

/* Each level's own file with one more scenery object, of an id no bank has: the parse stops after the landscape,
   and the level must stop as unplayable. */
static void unknown_object(int level) {
    static const uint32_t bad = 0x12345678u;
    float at[3] = {0.0f, 0.0f, 0.0f};
    size_t n = 0;
    uint8_t *lev = with_scenery(level, &bad, 1, 1, at, &n);
    int stopped;
    if (!lev) {
        printf("FAIL an unknown object, level %d: no level file\n", level);
        failed = 1;
        return;
    }
    start(level, lev, n);
    stopped = G64LevelUnplayable();
    play(level, 30);
    stop();
    free(lev);
    if (!stopped) {
        printf("FAIL an unknown object as scenery: level %d plays\n", level);
        failed = 1;
    }
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    int sweep = argc > 3 && strcmp(argv[3], "sweep") == 0;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        printf("skipped: no ROM (scenery ROM [FRAMES [sweep [LEVEL COUNT]]])\n");
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
    banksIndex = g64_rombanks_build_game(rom, romSize);
    if (!banksIndex) {
        printf("FAIL no index of the ROM's banks\n");
        return 1;
    }
    if (argc > 5) {
        /* One case: scenery ROM FRAMES MODE LEVEL COUNT. */
        static uint32_t ids[MAXIDS];
        float at[3];
        int level = atoi(argv[4]);
        unsigned numIds = level_ids(level, 1, ids, MAXIDS, at);
        one("one case", level, ids, numIds, (unsigned)atoi(argv[5]), at, sweep);
    } else {
        static const unsigned docCounts[] = {64, 100, 104, 107, 128, 200};
        static const int levels[] = {29, 44, 45, 13, 5, 0, 8, 20, 36};
        static uint32_t ids[MAXIDS];
        float at[3];
        unsigned i, k, numIds;
        int level;
        /* The note's case: object bank 29 is level 35's own bank, in its fourth slot. */
        numIds = level_ids(35, 3, ids, MAXIDS, at);
        for (k = 0; k < sizeof docCounts / sizeof docCounts[0]; k++)
            one("object bank 29", 35, ids, numIds, docCounts[k], NULL, sweep);
        present = 0xFFFFu;
        one("object bank 29 with 16 players", 35, ids, numIds, 128, NULL, sweep);
        present = 3;
        for (i = 0; i < sizeof levels / sizeof levels[0]; i++) {
            sweep_level(levels[i], 1000, sweep);
        }
        for (level = 0; level < LEVELS; level++)
            unknown_object(level);
        printf("%s every level stops as unplayable with an unknown object as scenery\n", failed ? "FAIL" : "ok  ");
    }
    printf("%s many scenery objects play or stop as unplayable\n", failed ? "FAIL" : "ok  ");
    g64_rombanks_free(banksIndex);
    free(withBuf);
    free(boot);
    engine_free(engine);
    return failed;
}
