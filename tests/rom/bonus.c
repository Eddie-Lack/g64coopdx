/* Bonus levels with more than one player (weemitt's bonus.c), online, with the ROM. Each case builds a level from
 * the state right after power-on, with two players, and runs it until both players are in the level.
 * Online a player wins or loses a bonus level alone: they take the level's exit, and the other player plays on.
 *   A. The Prehistoric bonus level: a player whose ball passes the finish wins, alone.
 *   B. The same level: a player whose ball falls out of the level loses, alone.
 *   C. The Atlantis bonus level: a player whose glove leaves the level's range loses, alone.
 * Each case runs once for player 0 and once for player 1, and both give the same record but for the player.
 *   D. The Carnival bonus level has one cannon. Every player sits on it, either player's stick turns it and either
 *      player's A button fires it.
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
int G64BulletPositions(float *xyz, int max);
int G64SoundListener(int p, float *pos, float *yaw, int *maxDist);
typedef struct {
    int player, type, complete, level, world;
    G64Save save;
} G64ExitRecord;
extern G64ExitRecord g64Exits[16];
extern int g64ExitCount;
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

/* The frame at which a case starts. Both players are in the level then, and neither has lost yet: without a hand on
   the controls a bonus level is soon lost. */
static unsigned READY;

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

static void pair(const char *name, int level, unsigned from, int kind, int complete) {
    Outcome a, b;
    char what[260];
    READY = from;
    start(level);
    check(G64HasPlayer(0) && G64HasPlayer(1) && g64ExitCount == 0, "both players are in the level");
    a = run(kind, 0);
    b = run(kind, 1);
    snprintf(what, sizeof what, "%s: player 0: frame %d, %d record(s), player %d, complete %d, the other stays %d; "
             "player 1: frame %d, %d record(s), player %d, complete %d, the other stays %d", name, a.at, a.count,
             a.player, a.complete, a.otherHere, b.at, b.count, b.player, b.complete, b.otherHere);
    check(a.at >= 0 && b.at >= 0 && a.count == 1 && b.count == 1 && a.player == 0 && b.player == 1 &&
          a.type == b.type && a.complete == complete && b.complete == complete && a.otherHere && b.otherHere, what);
    printf("%s: hash %08x\n", name, port_hash_ram());
}

/* D: 90 frames from the ready state of the Carnival bonus level. Player `turns` holds the stick to the right for
   the first 60, and player `fires` then presses A (-1: nobody). Returns the number of bullets that appear. glove
   gets where each player's glove ends, and yaw where each player's camera looks. */
static int cannon(int turns, int fires, float glove[2][3], float *yaw) {
    float b[64 * 3], cam[3];
    int max, shots = 0, before, now, p;
    unsigned f;
    engine_load(engine, ready);
    before = G64BulletPositions(b, 64);
    for (f = 0; f < 90; f++) {
        EngineInput in;
        G64SetPresent(0x3);
        for (p = 0; p < 16; p++) {
            g64Buttons[p] = p == fires && f == 70 ? 0x8000 : 0;
            g64StickX[p] = p == turns && f < 60 ? 60 : 0;
            g64StickY[p] = 0;
        }
        memset(&in, 0, sizeof in);
        in.buttons = g64Buttons[0];
        in.x = g64StickX[0];
        engine_step(engine, &in);
        now = G64BulletPositions(b, 64);
        if (now > before)
            shots += now - before;
        before = now;
    }
    for (p = 0; p < 2; p++) {
        glove[p][0] = glove[p][1] = glove[p][2] = 0;
        G64PlayerPosition(p, &glove[p][0], &glove[p][1], &glove[p][2]);
        yaw[p] = 0;
        G64SoundListener(p, cam, &yaw[p], &max);
    }
    return shots;
}

static float apart(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

static float turned(float a, float b) {
    float d = fmodf(fabsf(a - b), 6.2831853f);
    return d > 3.1415927f ? 6.2831853f - d : d;
}

static void case_d(void) {
    float still[2][3], g0[2][3], g1[2][3], y[2], y0[2], y1[2];
    int none, shots0, shots1;
    char what[300];
    READY = 400;
    start(19);
    check(G64HasPlayer(0) && G64HasPlayer(1), "D: both players are in the Carnival bonus level");
    none = cannon(-1, -1, still, y);
    shots0 = cannon(0, 0, g0, y0);
    shots1 = cannon(1, 1, g1, y1);
    snprintf(what, sizeof what, "D: the gloves are %.0f apart on the cannon; player 0's stick turns the cameras %.2f "
             "and %.2f, player 1's %.2f and %.2f", apart(still[0], still[1]), turned(y0[0], y[0]),
             turned(y0[1], y[1]), turned(y1[0], y[0]), turned(y1[1], y[1]));
    check(apart(still[0], still[1]) < 1.0f && turned(y0[0], y[0]) > 0.3f && turned(y0[1], y[1]) > 0.3f &&
          fabsf(turned(y1[0], y[0]) - turned(y0[0], y[0])) < 0.05f &&
          fabsf(turned(y1[1], y[1]) - turned(y0[1], y[1])) < 0.05f, what);
    snprintf(what, sizeof what, "D: with nobody firing %d bullets appear, with player 0 firing %d, with player 1 "
             "firing %d", none, shots0, shots1);
    check(shots0 == none + 1 && shots1 == none + 1, what);
    printf("D: hash %08x\n", port_hash_ram());
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
    pair("A", 29, 100, 0, 1);
    pair("B", 29, 100, 1, 0);
    pair("C", 14, 60, 2, 0);
    case_d();
    printf("%s each player wins or loses a bonus level alone\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
