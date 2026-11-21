/* Bonus levels with more than one player (weemitt's bonus.c), online, with the ROM. Each case builds a level from
 * the state right after power-on, with two players, and runs it until the second player's slot exists (frame 400).
 * Online a player wins or loses a bonus level alone: they take the level's exit, and the other player plays on.
 *   A. The Prehistoric bonus level: a player whose ball passes the finish wins, alone.
 *   B. The same level: a player whose ball falls out of the level loses, alone.
 *   C. The Atlantis bonus level: a player whose glove leaves the level's range loses, alone.
 * Each case runs once for player 0 and once for player 1, and both give the same record but for the player.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: bonus ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
int G64PlayerPosition(int p, float *x, float *y, float *z);
void G64PlacePlayer(int p, float x, float y, float z);
int G64BallPosition(int p, float *x, float *y, float *z);
void G64PlaceBall(int p, float x, float y, float z);
int G64HasPlayer(int p);
typedef struct {
    int player, type, complete, level, world;
    G64Save save;
} G64ExitRecord;
extern G64ExitRecord g64Exits[16];
extern int g64ExitCount;
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

/* The frame from which both players are in the game in every level used here. */
enum { READY = 400 };

static Engine *engine;
static void *boot, *ready;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* One frame with these players, who stand still. A presses close the tips. */
static void frame(unsigned f, unsigned present) {
    EngineInput in;
    int p;
    G64SetPresent(present);
    for (p = 0; p < 16; p++) {
        g64Buttons[p] = f < 300 && f % 30 == 0 ? 0x8000 : 0;
        g64StickX[p] = g64StickY[p] = 0;
    }
    memset(&in, 0, sizeof in);
    in.buttons = g64Buttons[0];
    engine_step(engine, &in);
}

/* A level from the power-on state with players 0 and 1, run to READY and kept in `ready`. */
static void start(int level) {
    G64Save s;
    unsigned f;
    engine_load(engine, boot);
    g64Online = 1;
    G64StartPlayers(0x3);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
    for (f = 0; f < READY; f++)
        frame(f, 0x3);
    engine_save(engine, ready);
}

/* What a run found: the frame of the first exit record, or -1, and that record's player, type and whether it
   completed the level; whether a record named the other player; whether the other player is still in the game. */
typedef struct {
    int at, player, type, complete, count, otherHere;
} Outcome;

/* kind 0: player `who`'s ball is put past the Prehistoric bonus level's finish. kind 1: their ball is put far below
   the level. kind 2: their glove is held outside the Atlantis bonus level's range. Runs up to 400 frames from the
   ready state, to the frame after the first record. */
static Outcome run(int kind, int who) {
    Outcome o = {-1, -1, -1, -1, 0, 0};
    float at[3] = {0, 0, 0};
    int f;
    engine_load(engine, ready);
    for (f = 0; f < 400; f++) {
        frame(READY + (unsigned)f, 0x3);
        if (o.at >= 0) {
            o.otherHere = G64HasPlayer(!who);
            break;
        }
        if (g64ExitCount > 0) {
            o.at = f;
            o.count = g64ExitCount;
            o.player = g64Exits[0].player;
            o.type = g64Exits[0].type;
            o.complete = g64Exits[0].complete;
            continue;
        }
        if (kind == 0 && f == 0) {
            G64BallPosition(who, &at[0], &at[1], &at[2]);
            G64PlaceBall(who, at[0], at[1] + 20.0f, 3760.0f);
        } else if (kind == 1 && f == 0) {
            G64BallPosition(who, &at[0], &at[1], &at[2]);
            G64PlaceBall(who, at[0], at[1] - 3000.0f, at[2]);
        } else if (kind == 2) {
            G64PlayerPosition(who, &at[0], &at[1], &at[2]);
            G64PlacePlayer(who, at[0], at[1], 9000.0f);
        }
    }
    return o;
}

static void pair(const char *name, int level, int kind, int complete) {
    Outcome a, b;
    char what[260];
    start(level);
    a = run(kind, 0);
    b = run(kind, 1);
    snprintf(what, sizeof what, "%s: player 0: frame %d, %d record(s), player %d, complete %d, the other stays %d; "
             "player 1: frame %d, %d record(s), player %d, complete %d, the other stays %d", name, a.at, a.count,
             a.player, a.complete, a.otherHere, b.at, b.count, b.player, b.complete, b.otherHere);
    check(a.at >= 0 && b.at >= 0 && a.count == 1 && b.count == 1 && a.player == 0 && b.player == 1 &&
          a.type == b.type && a.complete == complete && b.complete == complete && a.otherHere && b.otherHere, what);
    printf("%s: hash %08x\n", name, port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (bonus ROM)\n");
        return 0;
    }
    fclose(rom);
    engine_default_options(&opt);
    engine = engine_create(argv[1], &opt, err, sizeof err);
    if (!engine) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(engine));
    ready = malloc(engine_state_size(engine));
    if (!boot || !ready)
        return 1;
    engine_save(engine, boot);
    pair("A", 29, 0, 1);
    pair("B", 29, 1, 0);
    pair("C", 14, 2, 0);
    printf("%s each player wins or loses a bonus level alone\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
