/* May a pack's own models be platforms? The test of docs/discovery/model-banks.md, step 5, with the ROM.
 *
 * A rotation key of all zeros makes QuaternionToMatrix divide by zero. The rotation part of the matrix is then NaN
 * (not a number), and guMtxF2L turns each NaN into an integer, which C leaves undefined. Platforms, water and debris
 * read their object's first rotation key when they are drawn. Landscape and background pieces do not: they are
 * drawn with a static matrix made from the level file's rotation.
 *
 * The test builds a pack from scratch (tests/net/bankbuild.h): one 8 by 8 texture, a cube and a flat plate. Nothing
 * in it comes from the ROM. It has two variants of the same pack:
 *   I  each object's rotation key is the identity (0, 0, 0, 1). The bank check accepts it.
 *   Z  each key is all zeros, as Glovebox writes today. The bank check refuses it. The test loads it anyway, by
 *      setting the game's pack globals directly. This is a measurement only.
 *
 * The cases put the pack's objects in level 13, near the glove's place in the level's first frame (read from a run
 * without the pack; the level file has no HAND_START): two PLATFORMs with a spin and a path, a ROPEBRIDGE, a
 * SINEBRIDGE, an ORBITBRIDGE, a destructible platform whose debris uses the cube (the test breaks it at frame
 * DEBRIS_AT by setting its flag in the game's memory), and a WATER actor. A LANDSCAPE and a BACKGROUND
 * piece are controls. Each edited level file passes g64_lev_check. The test makes the plate's collision mesh a square,
 * 40 units on a side. One of the two PLATFORMs, the ROPEBRIDGE's first plank and the SINEBRIDGE's first plank start
 * under the glove.
 *
 * Each case runs FRAMES frames, with 2 and with 16 players who press buttons at random (the seeds of bots.c). Every
 * 100 frames it prints the memory hash (port_hash_ram), the number of NaN words in port_ram and the bits of the
 * first one, the number of words that hold the NaN of 0 times infinity, and the number of object matrix words that
 * a NaN gives (0x80008000, the integer half of two entries of -32768). It also prints a hash of objectMtxMore, the
 * object matrices past the first 256, which port_hash_ram does not cover. The NaN floats do not stay in memory, but
 * the converted matrices do. So the test also counts, in each frame's matrices, the matrices whose rotation part
 * holds those words, and apart from them the other words of 0x80008000 (two entries out of range, such as a far
 * translation). At frame SAVE_AT it
 * saves the game, runs 100 frames, loads the save and runs the 100 frames again. The two runs must give the same
 * hash on every frame.
 * In variant I no case may draw a NaN.
 *
 * Guards, in both variants: the case's actors are in the level, the level never stops as unplayable, and calcMtx
 * stays 0. In the PLATFORM and the two bridge cases with a plank under the glove, a glove or a ball must stand on
 * or touch one of the pack's platforms. In variant Z the matrices must show the NaN in every case but the two
 * controls. That proves those cases drew their actors.
 *
 * make models runs both variants on Linux and under Wine. Variant I must pass, and its outputs must be equal.
 * Variant Z only prints its exit statuses and whether its outputs are equal.
 *
 * Usage: models ROM I|Z [FRAMES]. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../net/bankbuild.h"
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
extern int g64PlanesBlocks;
extern unsigned char port_ram[];
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
void G64SetStartRecord(int world, const G64Save *s);
void G64SetPack(int level, unsigned rom, unsigned size);
void G64SetPackModels(unsigned texRom, unsigned texSize, unsigned objRom, unsigned objSize);
const void *G64PackBankLoaded(int objects, unsigned *word);
unsigned G64LevelFileRom(int num, unsigned *size);
int G64LevelUnplayable(void);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum {
    LEVEL = 13, QUIET = 300, SAVE_AT = 1000, REPLAY = 100, DEBRIS_AT = 995, RAM_BYTES = 0x1800000,
    T_PLATFORM = 98, T_DESTRUCTIBLE = 101, T_POINT = 107, T_SPEED = 108, T_ACCEL = 109, T_SPIN = 127,
    T_ROPEBRIDGE = 141, T_ORBITBRIDGE = 143, T_SINEBRIDGE = 144, T_BACKGROUND = 145, T_LANDSCAPE = 146,
    T_WATER = 160, T_END = 0x7D00
};
#define BOT_BUTTONS 0xE01Fu
#define TEX_ID 0x0A000001u
#define CUBE_ID 0x0B000001u
#define FLAT_ID 0x0B000002u
/* The integer half of two matrix entries made from NaN: (s32)NaN is 0x80000000 on x86. */
#define NAN_MTX_WORD 0x80008000u

/* The game's structures, at their N64 offsets (weemitt's include/actor.h and include/platform.h). */
enum {
    ACTOR_NEXT = 0x00, ACTOR_TYPE = 0x20, ACTOR_POS = 0x34, ACTOR_INST = 0xC8, PLATFORM_FLAGS = 0x14,
    PLATFORM_DESTRUCT = 0x24, PLATFORM_ACTOR = 0x80, ACTOR_ROT_QUAT = 0x9C,
    PLATFORM_LIST_COUNT = 0x00, PLATFORM_LIST_HEAD = 0x04, DYNAMIC_OBJECT_MTX = 0x80, MTX_BYTES = 64,
    OBJECT_MTX = 256, OBJECT_MTX_MORE = 768, ACTOR_OBJ = 0xD8, OBJ_PLATFORM = 0x178, OBJ_CONTACT_PLATFORM = 0x180
};
extern unsigned char objectMtxMore[];

static Engine *engine;
static void *boot, *saved;
static const uint8_t *rom;
static unsigned long romSize;
static unsigned frames = 3000;
static int failed, variantZ;
static uint32_t symActors, symPlatforms, symCalcMtx, symDynamic, symHand, symBall;
/* The pack's object bank in the game's memory, while a case runs: [packLo, packHi). */
static uint32_t packLo, packHi;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    fflush(stdout);
    failed |= !ok;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* ---- The pack. */

static BB bb;
static uint8_t packI[1 << 16], packZ[1 << 16];
static size_t packN;
static G64Models models;

/* Makes the flat plate's collision mesh a square deck, 40 units on a side, centered on the object's origin. Its
   faces are two triangles, each in both windings, then the same four again. The builder's own mesh is made of thin
   triangles, which a glove standing on the plate seldom touches. The display list is left as it is. A glove on a
   rope bridge's plank of this mesh touches 4 faces at once. Before collision.c bounded its loop over the touched
   planes, that crashed the game; the ROPEBRIDGE case is its test. */
static void make_deck(uint32_t flat) {
    static const float corner[4][2] = {{-20.0f, -20.0f}, {20.0f, -20.0f}, {20.0f, 20.0f}, {-20.0f, 20.0f}};
    static const uint8_t face[4][3] = {{0, 1, 2}, {0, 2, 3}, {0, 2, 1}, {0, 3, 2}};
    uint32_t mesh = be32(bb.obj + flat + 0x14), verts = be32(bb.obj + mesh + 4), faces = be32(bb.obj + mesh + 8), i;
    for (i = 0; i < 4; i++) {
        bb_putf(bb.obj + verts + 12 * i, corner[i][0]);
        bb_putf(bb.obj + verts + 12 * i + 4, 0.0f);
        bb_putf(bb.obj + verts + 12 * i + 8, corner[i][1]);
    }
    for (i = 0; i < 8 * 3; i++)
        bb_put16(bb.obj + faces + 2 * i, face[(i / 3) % 4][i % 3]);
}

/* Builds both variants. The rotation key is the third block of an object (bankbuild.h): x, y, z, w, then the time.
   The builder writes the identity; variant Z sets the four values of the key to 0. */
static int build_pack(void) {
    BBObject s;
    uint32_t cube, flat, rot;
    size_t at = 0;
    const char *why;

    bb_begin(&bb, 2);
    bb_texture(&bb, TEX_ID, 8, 8, 0);
    memset(&s, 0, sizeof s);
    s.name = "g64cube";
    s.verts = 8;
    s.faces = 12;
    s.ntex = 1;
    s.tex[0] = TEX_ID;
    cube = bb_object(&bb, &s);
    s.name = "g64flat";
    s.verts = 10;
    s.faces = 8;
    s.flat = 1;
    flat = bb_object(&bb, &s);
    make_deck(flat);
    bb_entry(&bb, CUBE_ID, cube, 1);
    bb_entry(&bb, FLAT_ID, flat, 1);
    packN = bb_end(&bb, packI, sizeof packI, 0, NULL, 0, NULL, 0);
    if (packN == 0)
        return 0;
    memcpy(packZ, packI, packN);
    why = g64_models_check(packI, packN, NULL, NULL, &models, &at);
    check(why == NULL, why ? why : "variant I: the bank check accepts the pack");
    if (why)
        return 0;
    rot = be32(bb.obj + cube + 0x24);
    memset(packZ + models.objAt + rot, 0, 16);
    rot = be32(bb.obj + flat + 0x24);
    memset(packZ + models.objAt + rot, 0, 16);
    {
        G64Models mz;
        why = g64_models_check(packZ, packN, NULL, NULL, &mz, &at);
        check(why != NULL && strstr(why, "rotation key") != NULL, "variant Z: the bank check refuses the zero key");
    }
    return 1;
}

/* ---- The level files. */

static uint8_t tokens[4096];
static size_t tokenN;
/* Where the glove is in the level's first frame, without the pack. The level file has no HAND_START. */
static float hand[3];
static size_t endAt;
static int sawEnd;

static void scan(void *user, size_t offset, int token) {
    (void)user;
    if (token == T_END) {
        endAt = offset;
        sawEnd = 1;
    }
}

static void put16(int v) {
    bb_put16(tokens + tokenN, (uint32_t)(uint16_t)v);
    tokenN += 2;
}

static void put32(uint32_t v) {
    bb_put32(tokens + tokenN, v);
    tokenN += 4;
}

static void putf(float f) {
    bb_putf(tokens + tokenN, f);
    tokenN += 4;
}

/* An object's id and its 8 bytes of name. */
static void put_object(uint32_t id) {
    put32(id);
    memcpy(tokens + tokenN, id == CUBE_ID ? "g64cube" : "g64flat", 8);
    tokenN += 8;
}

/* A point near the glove's start. */
static void put_near(float dx, float dy, float dz) {
    putf(hand[0] + dx);
    putf(hand[1] + dy);
    putf(hand[2] + dz);
}

static void put_platform(uint32_t id, float dx, float dy, float dz) {
    put16(T_PLATFORM);
    put_object(id);
    put16(T_POINT);
    put16(0);
    put_near(dx, dy, dz);
}

/* The cases. */
/* Where the plates under the glove start, from the glove's place in the first frame. */
#define UNDER_Y -3.0f

enum { C_PLATFORM, C_ROPE, C_SINE, C_ORBIT, C_DEBRIS, C_WATER, C_LANDSCAPE, C_BACKGROUND, CASES };
static const char *const caseName[CASES] = {
    "PLATFORM with a spin and a path", "ROPEBRIDGE", "SINEBRIDGE", "ORBITBRIDGE",
    "destructible PLATFORM and its debris",
    "WATER", "LANDSCAPE (control)", "BACKGROUND (control)"};

static void case_tokens(int c) {
    int k;
    tokenN = 0;
    switch (c) {
    case C_PLATFORM:
        /* Two platforms, each with a spin on one axis and a path of two points. A path needs an acceleration. */
        put_platform(CUBE_ID, 40.0f, -30.0f, 0.0f);
        put16(T_POINT);
        put16(0);
        put_near(40.0f, -30.0f, 80.0f);
        put16(T_SPEED);
        putf(1.0f);
        put16(T_ACCEL);
        putf(0.1f);
        put16(T_SPIN);
        put16(1);
        putf(0.0f);
        putf(0.05f);
        /* The plate starts under the glove. */
        put_platform(FLAT_ID, 0.0f, UNDER_Y, 0.0f);
        put16(T_POINT);
        put16(0);
        put_near(0.0f, UNDER_Y, -60.0f);
        put16(T_SPEED);
        putf(0.5f);
        put16(T_ACCEL);
        putf(0.1f);
        put16(T_SPIN);
        put16(0);
        putf(0.0f);
        putf(0.03f);
        break;
    case C_ROPE:
        /* Planks, their inertia, the axis, the object, then stiffness, damping, acceleration, the limit, the
           start angle, the most speed and the limit's growth per plank, the first plank and the step. */
        put16(T_ROPEBRIDGE);
        put16(6);
        put16(1);
        put16(0);
        put_object(FLAT_ID);
        putf(0.1f);
        putf(0.9f);
        putf(0.0f);
        putf(0.5f);
        putf(0.0f);
        putf(0.1f);
        putf(0.05f);
        put_near(0.0f, UNDER_Y, 0.0f);
        putf(20.0f);
        putf(0.0f);
        putf(0.0f);
        break;
    case C_SINE:
        /* Planks, the head start of each, the object, the first plank, the step, the swing, speed and
           acceleration. */
        put16(T_SINEBRIDGE);
        put16(5);
        put16(4);
        put_object(FLAT_ID);
        put_near(0.0f, UNDER_Y, 0.0f);
        putf(20.0f);
        putf(0.0f);
        putf(0.0f);
        putf(0.0f);
        putf(30.0f);
        putf(0.0f);
        putf(1.0f);
        putf(0.1f);
        break;
    case C_ORBIT:
        /* The axis (Y), the planks, the head start, -1 for one offset, the object, the center, the step, the
           offset and the speed. */
        put16(T_ORBITBRIDGE);
        put16(0x59);
        put16(4);
        put16(5);
        put16(-1);
        put_object(CUBE_ID);
        put_near(0.0f, -10.0f, 0.0f);
        putf(25.0f);
        putf(0.0f);
        putf(0.0f);
        putf(30.0f);
        putf(0.0f);
        putf(0.0f);
        putf(0.05f);
        break;
    case C_DEBRIS:
        /* A platform that breaks into 12 pieces of the cube. */
        put_platform(FLAT_ID, 30.0f, -10.0f, 30.0f);
        put16(T_DESTRUCTIBLE);
        put16(0);
        put32(12);
        put_object(CUBE_ID);
        break;
    case C_WATER:
        /* The water's box and level are away from the glove. The water's actor is near it. */
        put16(T_WATER);
        put_near(200.0f, -200.0f, 200.0f);
        putf(50.0f);
        putf(50.0f);
        putf(50.0f);
        putf(hand[1] - 170.0f);
        putf(0.0f);
        putf(0.0f);
        put16(0);
        put_object(FLAT_ID);
        put_near(30.0f, -10.0f, 30.0f);
        break;
    case C_LANDSCAPE:
    case C_BACKGROUND:
        for (k = 0; k < 2; k++) {
            put16(c == C_LANDSCAPE ? T_LANDSCAPE : T_BACKGROUND);
            put_object(k ? FLAT_ID : CUBE_ID);
            put_near(k ? -40.0f : 40.0f, -20.0f, 30.0f);
        }
        break;
    }
}

/* A copy of the level's file with the case's tokens before the end token, in a new buffer. NULL when the copy does
   not pass the level check. */
static uint8_t *level_file(int c, size_t *n) {
    unsigned size = 0, at = G64LevelFileRom(LEVEL, &size);
    size_t at2 = 0, grown;
    const char *why;
    uint8_t *lev;
    if (at == 0 || size < 8 || at + size > romSize)
        return NULL;
    sawEnd = 0;
    why = g64_lev_check(rom + at, size, scan, (void *)(rom + at), &at2);
    if (why != NULL || !sawEnd) {
        printf("FAIL the ROM's level file: %s\n", why ? why : "no end token");
        failed = 1;
        return NULL;
    }
    case_tokens(c);
    grown = size + tokenN;
    lev = calloc(1, grown);
    if (!lev)
        return NULL;
    memcpy(lev, rom + at, endAt);
    memcpy(lev + endAt, tokens, tokenN);
    memcpy(lev + endAt + tokenN, rom + at + endAt, size - endAt);
    bb_put32(lev, (uint32_t)grown);
    why = g64_lev_check(lev, grown, NULL, NULL, &at2);
    if (why != NULL) {
        printf("FAIL %s: the level check refuses the file: %s\n", caseName[c], why);
        failed = 1;
        free(lev);
        return NULL;
    }
    *n = grown;
    return lev;
}

/* ---- A game with the pack. */

static uint8_t *withBuf;

static void start(const uint8_t *lev, size_t levN, const uint8_t *file, int players) {
    G64Save s;
    unsigned long total = romSize + levN + models.texSize + models.objSize;
    unsigned mask = players >= 16 ? 0xFFFFu : (1u << players) - 1;
    free(withBuf);
    withBuf = malloc(total);
    if (!withBuf) {
        printf("FAIL out of memory\n");
        exit(1);
    }
    memcpy(withBuf, rom, romSize);
    memcpy(withBuf + romSize, lev, levN);
    memcpy(withBuf + romSize + levN, file + models.texAt, models.texSize);
    memcpy(withBuf + romSize + levN + models.texSize, file + models.objAt, models.objSize);
    packLo = packHi = 0;
    engine_load(engine, boot);
    engine_set_rom(engine, withBuf, total);
    G64SetPack(LEVEL, (unsigned)romSize, (unsigned)levN);
    G64SetPackModels((unsigned)(romSize + levN), models.texSize, (unsigned)(romSize + levN + models.texSize),
                     models.objSize);
    g64Online = 1;
    G64StartPlayers(mask);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    g64PlanesBlocks = -1;
    port_game_start_level(LEVEL);
}

/* The players' generator and held inputs, as tests/rom/bots.c makes them. The save keeps a copy. */
typedef struct {
    unsigned seed, frame;
    unsigned short buttons[16];
    signed char sx[16], sy[16];
} Bots;

static unsigned next(Bots *b) {
    b->seed = b->seed * 1664525u + 1013904223u;
    return b->seed >> 8;
}

/* What the frames showed: the level stopped, calcMtx was not 0, the most NaN matrices in a frame's tables, the
   frames with 0x80008000 words that are not in a NaN matrix, and the frames in which a glove or a ball stood on or
   touched one of the pack's platforms. */
typedef struct {
    int stopped, calc;
    unsigned mostMtx, framesMtx, framesBroken, firstBroken, lastBroken;
    unsigned framesOtherWords, mostOtherWords;
    unsigned framesHandOn, framesBallOn;
} Seen;

/* Whether a platform's actor is drawn with one of the pack's objects. */
static int pack_platform(uint32_t p) {
    uint32_t inst;
    if (!p || !packLo)
        return 0;
    inst = engine_readptr(engine, p + PLATFORM_ACTOR + ACTOR_INST);
    return inst >= packLo && inst < packHi;
}

/* Whether an actor stands on or touches one of the pack's platforms. */
static int on_pack_platform(uint32_t a) {
    uint32_t obj = engine_readptr(engine, a + ACTOR_OBJ);
    if (!obj)
        return 0;
    return pack_platform(engine_readptr(engine, obj + OBJ_PLATFORM)) ||
           pack_platform(engine_readptr(engine, obj + OBJ_CONTACT_PLATFORM));
}

/* The same for every glove and ball. The other players' gloves and balls are proxies in the actor list, of the
   types of the first player's. */
static void note_contacts(Seen *seen) {
    uint32_t head = symActors, a;
    int16_t th = (int16_t)engine_read16(engine, symHand + ACTOR_TYPE);
    int16_t tb = (int16_t)engine_read16(engine, symBall + ACTOR_TYPE), t;
    int hand = on_pack_platform(symHand), ball = on_pack_platform(symBall), guard = 0;
    for (a = engine_readptr(engine, head + ACTOR_NEXT); a && a != head && guard++ < 100000;
         a = engine_readptr(engine, a + ACTOR_NEXT)) {
        t = (int16_t)engine_read16(engine, a + ACTOR_TYPE);
        if (t == th && !hand)
            hand = on_pack_platform(a);
        else if (t == tb && !ball)
            ball = on_pack_platform(a);
    }
    seen->framesHandOn += hand;
    seen->framesBallOn += ball;
}

/* Counts one matrix of 16 words. A matrix is NaN when its rotation is: guMtxF2L puts the integer halves of columns
   0 and 1 of rows 0, 1 and 2 in words 0, 2 and 4. Other words of 0x80008000 also come from two entries out of the
   range of an s15.16 number, such as a far translation (word 6). */
static void count_mtx(const uint32_t *w, unsigned *nanMtx, unsigned *other) {
    unsigned k, nan = w[0] == NAN_MTX_WORD && w[2] == NAN_MTX_WORD && w[4] == NAN_MTX_WORD;
    *nanMtx += nan;
    if (!nan)
        for (k = 0; k < 16; k++)
            *other += w[k] == NAN_MTX_WORD;
}

/* The NaN matrices of the frame's object matrices, the 256 in Dynamic and those in objectMtxMore, and the other
   words of 0x80008000 in them. */
static unsigned nan_mtx(unsigned *other) {
    uint32_t dyn = engine_readptr(engine, symDynamic), i, k, w[16];
    unsigned n = 0;
    *other = 0;
    if (dyn)
        for (i = 0; i < OBJECT_MTX; i++) {
            for (k = 0; k < 16; k++)
                w[k] = engine_read32(engine, dyn + DYNAMIC_OBJECT_MTX + i * MTX_BYTES + 4 * k);
            count_mtx(w, &n, other);
        }
    for (i = 0; i < 2u * OBJECT_MTX_MORE; i++) {
        memcpy(w, objectMtxMore + i * MTX_BYTES, MTX_BYTES);
        count_mtx(w, &n, other);
    }
    return n;
}

/* Breaks every platform that can break (UpdatePlatforms then makes its debris). */
static void break_platforms(void) {
    uint32_t head = symPlatforms + PLATFORM_LIST_HEAD, p = engine_readptr(engine, head);
    int guard = 0;
    while (p && p != head && guard++ < 4096) {
        if (engine_readptr(engine, p + PLATFORM_DESTRUCT))
            engine_write(engine, p + PLATFORM_FLAGS, 4, engine_read32(engine, p + PLATFORM_FLAGS) | 2);
        p = engine_readptr(engine, p);
    }
}

static void step(Bots *b, int players, int breakAt, Seen *seen) {
    unsigned mask = players >= 16 ? 0xFFFFu : (1u << players) - 1, m, other;
    EngineInput in;
    int p;
    if (breakAt && b->frame == DEBRIS_AT)
        break_platforms();
    G64SetPresent(mask);
    for (p = 0; p < 16; p++) {
        if (p >= players) {
            g64Buttons[p] = 0;
            g64StickX[p] = g64StickY[p] = 0;
            continue;
        }
        if (b->frame < QUIET) {
            b->buttons[p] = b->frame % 30 == 0 ? 0x8000 : 0;
        } else if (next(b) % 12 == 0) {
            b->sx[p] = (signed char)((int)(next(b) % 161) - 80);
            b->sy[p] = (signed char)((int)(next(b) % 161) - 80);
            b->buttons[p] = next(b) % 3 == 0 ? (unsigned short)(next(b) & BOT_BUTTONS) : 0;
        }
        g64Buttons[p] = b->buttons[p];
        g64StickX[p] = b->sx[p];
        g64StickY[p] = b->sy[p];
    }
    memset(&in, 0, sizeof in);
    in.buttons = g64Buttons[0];
    in.x = g64StickX[0];
    in.y = g64StickY[0];
    engine_step(engine, &in);
    b->frame++;
    seen->stopped |= G64LevelUnplayable();
    seen->calc |= engine_read8(engine, symCalcMtx) != 0;
    m = nan_mtx(&other);
    if (m > seen->mostMtx)
        seen->mostMtx = m;
    seen->framesMtx += m != 0;
    if (other > seen->mostOtherWords)
        seen->mostOtherWords = other;
    seen->framesOtherWords += other != 0;
    note_contacts(seen);
    /* After the break the platform is gone, and only its debris can draw the cube. */
    if (breakAt && b->frame > DEBRIS_AT + 1 && m != 0) {
        if (!seen->framesBroken++)
            seen->firstBroken = b->frame;
        seen->lastBroken = b->frame;
    }
}

/* A hash (FNV-1a) of objectMtxMore: the object matrices past the 256 in Dynamic. They are in port_win, outside the
   0x1800000 bytes that port_hash_ram covers. */
static uint32_t more_hash(void) {
    uint32_t h = 2166136261u, i;
    for (i = 0; i < 2u * OBJECT_MTX_MORE * MTX_BYTES; i++)
        h = (h ^ objectMtxMore[i]) * 16777619u;
    return h;
}

/* The NaN words in port_ram, the bits of the first and its offset, the NaN matrix words anywhere in it, and the hash
   of objectMtxMore. */
static void ram_line(const char *label, unsigned frame) {
    uint32_t i, w, first = 0, firstAt = 0, nans = 0, made = 0, mtx = 0;
    for (i = 0; i < RAM_BYTES; i += 4) {
        memcpy(&w, port_ram + i, 4);
        if ((w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu) != 0) {
            if (nans++ == 0) {
                first = w;
                firstAt = i;
            }
        }
        /* The NaN that x86 makes for 0 times infinity. */
        made += w == 0xFFC00000u || w == 0x7FC00000u;
        mtx += w == NAN_MTX_WORD;
    }
    printf("  %sframe %4u hash %08x nan %u first %08x at %06x made %u mtx %u more %08x\n", label, frame,
           port_hash_ram(), nans, first, firstAt, made, mtx, more_hash());
}

/* The pack's actors in the level, by type. */
static void count_actors(const char *name, int *total) {
    unsigned word = 0;
    const void *bank = G64PackBankLoaded(1, &word);
    uint32_t lo = bank ? engine_host_to_n64(bank) : 0, hi = lo + models.objSize, head = symActors, a, inst;
    int counts[64], t, guard = 0;
    memset(counts, 0, sizeof counts);
    *total = 0;
    for (a = engine_readptr(engine, head + ACTOR_NEXT); a && a != head && guard++ < 100000;
         a = engine_readptr(engine, a + ACTOR_NEXT)) {
        inst = engine_readptr(engine, a + ACTOR_INST);
        t = (int16_t)engine_read16(engine, a + ACTOR_TYPE);
        if (lo && inst >= lo && inst < hi && t >= 0 && t < 64) {
            counts[t]++;
            (*total)++;
        }
    }
    printf("  %s: the pack's actors:", name);
    for (t = 0; t < 64; t++)
        if (counts[t])
            printf(" type %d x %d", t, counts[t]);
    printf("%s\n", *total ? "" : " none");
}

static int platform_count(void) {
    return (int)engine_read32(engine, symPlatforms + PLATFORM_LIST_COUNT);
}

/* The places and rotations of the pack's platforms, as words: 7 for each, at most 8 platforms. Returns the count. */
static int pack_platforms(uint32_t *out) {
    unsigned word = 0;
    const void *bank = G64PackBankLoaded(1, &word);
    uint32_t lo = bank ? engine_host_to_n64(bank) : 0, head = symPlatforms + PLATFORM_LIST_HEAD, p, a, inst, k;
    int n = 0, guard = 0;
    for (p = engine_readptr(engine, head); lo && p && p != head && guard++ < 4096 && n < 8;
         p = engine_readptr(engine, p)) {
        a = p + PLATFORM_ACTOR;
        inst = engine_readptr(engine, a + ACTOR_INST);
        if (inst < lo || inst >= lo + models.objSize)
            continue;
        for (k = 0; k < 3; k++)
            out[7 * n + k] = engine_read32(engine, a + ACTOR_POS + 4 * k);
        for (k = 0; k < 4; k++)
            out[7 * n + 3 + k] = engine_read32(engine, a + ACTOR_ROT_QUAT + 4 * k);
        n++;
    }
    return n;
}

/* Starts the level without the pack and reads the glove's place in its first frame. */
static int probe_hand(void) {
    Bots b;
    Seen seen;
    G64Save rec;
    uint32_t h = engine_symbol("hand"), k;
    if (!h)
        return 0;
    engine_load(engine, boot);
    G64SetPack(-1, 0, 0);
    G64SetPackModels(0, 0, 0, 0);
    g64Online = 1;
    G64StartPlayers(3);
    memset(&rec, 0, sizeof rec);
    rec.levels = 1;
    G64SetStartRecord(0, &rec);
    port_game_start_level(LEVEL);
    memset(&b, 0, sizeof b);
    memset(&seen, 0, sizeof seen);
    step(&b, 2, 0, &seen);
    for (k = 0; k < 3; k++)
        hand[k] = engine_readf(engine, h + ACTOR_POS + 4 * k);
    printf("the glove starts at %.1f %.1f %.1f\n", hand[0], hand[1], hand[2]);
    return 1;
}

static void run_case(int c, int players) {
    uint8_t *lev;
    size_t levN = 0;
    unsigned hashes[REPLAY], h0, k;
    int actors = 0, before = 0, after = 0, control = c == C_LANDSCAPE || c == C_BACKGROUND, equal = 1;
    Bots b, keep;
    Seen seen;
    char line[300];
    uint32_t plat0[7 * 8], plat1[7 * 8];
    int nplat, moved[8] = {0}, turned[8] = {0};

    printf("%s, %d players\n", caseName[c], players);
    lev = level_file(c, &levN);
    if (!lev) {
        check(0, "the case's level file");
        return;
    }
    start(lev, levN, variantZ ? packZ : packI, players);
    free(lev);
    memset(&b, 0, sizeof b);
    memset(&seen, 0, sizeof seen);
    b.seed = 12345u + LEVEL;
    step(&b, players, 0, &seen);
    {
        unsigned word = 0;
        const void *bank = G64PackBankLoaded(1, &word);
        packLo = bank ? engine_host_to_n64(bank) : 0;
        packHi = packLo + models.objSize;
    }
    count_actors(caseName[c], &actors);
    nplat = pack_platforms(plat0);
    snprintf(line, sizeof line, "%s, %d players: the pack's objects are in the level", caseName[c], players);
    check(actors > 0, line);
    while (b.frame < frames) {
        if (c == C_DEBRIS && b.frame == DEBRIS_AT)
            before = platform_count();
        step(&b, players, c == C_DEBRIS, &seen);
        if (c == C_DEBRIS && b.frame == DEBRIS_AT + 1) {
            after = platform_count();
            snprintf(line, sizeof line, "%s, %d players: the platform broke (platforms %d, then %d)", caseName[c],
                     players, before, after);
            check(after < before, line);
        }
        if (b.frame % 100 == 0) {
            ram_line("", b.frame);
            /* Whether each platform has left its first place or turned, at any sample. */
            if (c == C_PLATFORM && pack_platforms(plat1) == nplat)
                for (k = 0; k < (unsigned)nplat; k++) {
                    moved[k] |= memcmp(plat0 + 7 * k, plat1 + 7 * k, 12) != 0;
                    turned[k] |= memcmp(plat0 + 7 * k + 3, plat1 + 7 * k + 3, 16) != 0;
                }
        }
        if (b.frame == SAVE_AT) {
            if (!engine_save(engine, saved)) {
                check(0, "the save");
                return;
            }
            keep = b;
            h0 = port_hash_ram();
            for (k = 0; k < REPLAY; k++) {
                step(&b, players, c == C_DEBRIS, &seen);
                hashes[k] = port_hash_ram();
            }
            check(engine_load(engine, saved) == 0, "the load");
            b = keep;
            equal = port_hash_ram() == h0;
            for (k = 0; k < REPLAY; k++) {
                step(&b, players, c == C_DEBRIS, &seen);
                equal &= port_hash_ram() == hashes[k];
            }
            snprintf(line, sizeof line, "%s, %d players: a save at frame %d and a load give the same %d frames",
                     caseName[c], players, SAVE_AT, REPLAY);
            check(equal, line);
            /* The replay ends on a sample frame, which the loop's own test has passed by. */
            if (b.frame % 100 == 0)
                ram_line("", b.frame);
        }
    }
    if (c == C_PLATFORM) {
        int i, nm = 0, nt = 0;
        for (i = 0; i < nplat; i++) {
            nm += moved[i];
            nt += turned[i];
        }
        snprintf(line, sizeof line, "%s, %d players: of %d platforms, %d moved and %d turned", caseName[c], players,
                 nplat, nm, nt);
        check(nplat == 2 && nm == nplat && nt == nplat, line);
    }
    snprintf(line, sizeof line, "%s, %d players: the level plays %u frames", caseName[c], players, frames);
    check(!seen.stopped, line);
    snprintf(line, sizeof line, "%s, %d players: calcMtx stays 0", caseName[c], players);
    check(!seen.calc, line);
    printf("  NaN matrices in a frame's tables: at most %u, in %u frames\n", seen.mostMtx, seen.framesMtx);
    printf("  other words of 0x80008000 in them: at most %u, in %u frames\n", seen.mostOtherWords,
           seen.framesOtherWords);
    printf("  frames with a glove on the pack's platforms: %u; with a ball: %u\n", seen.framesHandOn,
           seen.framesBallOn);
    /* Not the orbit bridge. The parser never calls InitPlatformInfo for its planks (parse.c, ORBITBRIDGE). So their
       collision radius stays 0, and their faces never get the normals that RecalculatePlatformInfo keeps for a
       platform that has no motion and no rotation. In this test no glove or ball touched one. */
    if (c == C_PLATFORM || c == C_ROPE || c == C_SINE) {
        snprintf(line, sizeof line, "%s, %d players: a glove or a ball touches the pack's platforms", caseName[c],
                 players);
        check(seen.framesHandOn + seen.framesBallOn > 0, line);
    }
    if (c == C_DEBRIS)
        printf("  after the break, frames with NaN matrices: %u, from frame %u to %u\n", seen.framesBroken,
               seen.firstBroken, seen.lastBroken);
    if (variantZ) {
        snprintf(line, sizeof line, "%s, %d players: the zero key %s the drawn matrices", caseName[c], players,
                 control ? "does not reach" : "reaches");
        check(control ? seen.framesMtx == 0 : seen.framesMtx > 0, line);
        if (c == C_DEBRIS) {
            snprintf(line, sizeof line, "%s, %d players: the debris draws the zero key", caseName[c], players);
            check(seen.framesBroken > 0, line);
        }
    } else {
        snprintf(line, sizeof line, "%s, %d players: no NaN reaches the drawn matrices", caseName[c], players);
        check(seen.framesMtx == 0, line);
    }
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    int c;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        printf("skipped: no ROM (models ROM I|Z [FRAMES])\n");
        return 0;
    }
    fclose(f);
    variantZ = argc > 2 && argv[2][0] == 'Z';
    if (argc > 3) {
        if (atoi(argv[3]) <= SAVE_AT + REPLAY) {
            printf("FAIL FRAMES must be more than %d\n", SAVE_AT + REPLAY);
            return 1;
        }
        frames = (unsigned)atoi(argv[3]);
    }
    printf("variant %s: %s\n", variantZ ? "Z" : "I",
           variantZ ? "rotation keys of all zeros, loaded past the bank check" : "identity rotation keys");
    engine_default_options(&opt);
    engine = engine_create(argv[1], &opt, err, sizeof err);
    if (!engine) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(engine));
    saved = malloc(engine_state_size(engine));
    if (!boot || !saved)
        return 1;
    engine_save(engine, boot);
    rom = engine_rom(engine, &romSize);
    symActors = engine_symbol("actorList");
    symPlatforms = engine_symbol("platformList");
    symCalcMtx = engine_symbol("calcMtx");
    symDynamic = engine_symbol("dynamicp");
    symHand = engine_symbol("hand");
    symBall = engine_symbol("ball");
    if (!symActors || !symPlatforms || !symCalcMtx || !symDynamic || !symHand || !symBall) {
        printf("FAIL the engine has no symbol for actorList, platformList, calcMtx, dynamicp, hand or ball\n");
        return 1;
    }
    if (!build_pack()) {
        printf("FAIL the pack\n");
        return 1;
    }
    if (!probe_hand()) {
        printf("FAIL the engine has no symbol for hand\n");
        return 1;
    }
    for (c = 0; c < CASES; c++) {
        run_case(c, 2);
        run_case(c, 16);
    }
    printf("%s variant %s: every case ran as the test expects\n", failed ? "FAIL" : "ok  ", variantZ ? "Z" : "I");
    free(withBuf);
    free(boot);
    free(saved);
    engine_free(engine);
    return failed;
}
