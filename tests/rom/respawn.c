/* Respawns with more than one player (weemitt's platform.c, RestartPlayer and CheckRestartGate, and g64players.c,
 * "Respawns"), online, with the ROM. Each case builds level 10 from the state right after power-on, runs it to frame
 * 400, and then makes a player fall under the level, which kills them.
 *   A. Player 1 falls. Their own countdown runs from 65 to 0, one step a frame, and they come back at the level's
 *      start. Player 0's countdown stays 0, and player 0 ends where they end when player 1 doesn't fall. Nobody
 *      loses a life.
 *   B. Player 1's ball falls through the level's second gate, which becomes their checkpoint and not player 0's.
 *      Player 1 then falls, and comes back at the second gate.
 *   C. While player 1 respawns, no enemy jumps back to its start. Offline, with one player, a respawn does restart
 *      the enemies: some enemy jumps.
 *   D. Player 1 stands over the ground at the second gate, far from player 0's camera, and then over nothing. Their
 *      glove's altitude follows them: small over the ground, -1 over nothing. A fall over nothing kills.
 *   E. Player 0 leaves at frame 5, during the level's opening respawn and before the other players' slots exist.
 *      Player 1 takes the globals and finishes the opening: their countdown goes on to 0.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: respawn ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
void G64PlaceBall(int p, float x, float y, float z);
int G64RestartCountdown(int p);
int G64GatePosition(int k, float *x, float *y, float *z);
int G64PlayerCheckpoint(int p);
int G64PlayerLives(int p);
float G64PlayerAltitude(int p);
int G64EnemyInfo(int i, float *x, float *y, float *z);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { LEVEL = 10, READY = 400, FALL = 600, END = 800 };

static Engine *engine;
static void *boot, *ready;
static int failed;
static unsigned present;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

static void frame(unsigned f) {
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

/* The level from the power-on state, with these players, online or not, run to READY and saved. */
static void start(unsigned players, int online) {
    G64Save s;
    unsigned f;
    engine_load(engine, boot);
    g64Online = online;
    present = players;
    G64StartPlayers(players);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(LEVEL);
    for (f = 0; f < READY; f++)
        frame(f);
    engine_save(engine, ready);
}

static float dist2d(const float *a, const float *b) {
    float x = a[0] - b[0], z = a[2] - b[2];
    return sqrtf(x * x + z * z);
}

static void where(int p, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64PlayerPosition(p, &at[0], &at[1], &at[2]);
}

/* What a run from READY to END saw. who falls at FALL (-1: nobody); gate >= 0: who's ball is first dropped on that
   gate. */
typedef struct {
    int steps, firstCountdown, otherMax, cp0, cp1, lives0, lives1;
    float end0[3], end1[3], maxJump;
} Run;

static void run(int who, int gate, Run *r) {
    float at[3], gx = 0, gy = 0, gz = 0, prev[30][3];
    int last = 0, k;
    unsigned f;
    engine_load(engine, ready);
    memset(r, 0, sizeof *r);
    r->firstCountdown = -1;
    where(who < 0 ? 0 : who, at);
    if (gate >= 0)
        G64GatePosition(gate, &gx, &gy, &gz);
    for (k = 0; k < 30; k++)
        G64EnemyInfo(k, &prev[k][0], &prev[k][1], &prev[k][2]);
    for (f = READY; f < END; f++) {
        int t, other;
        if (gate >= 0 && f < READY + 2) {
            G64PlacePlayer(who, gx + 30.0f, gy + 60.0f, gz);
            G64PlaceBall(who, gx, gy + 40.0f, gz);
        }
        if (who >= 0 && f == FALL)
            G64PlacePlayer(who, at[0], at[1] - 3000.0f, at[2]);
        frame(f);
        t = who >= 0 ? G64RestartCountdown(who) : 0;
        other = present == 0x3 ? G64RestartCountdown(who == 1 ? 0 : 1) : 0;
        if (other > r->otherMax)
            r->otherMax = other;
        if (t > 0 && r->firstCountdown < 0)
            r->firstCountdown = t;
        /* A countdown that runs one step a frame. */
        if (t > 0 && (last == 0 || t == last - 1))
            r->steps++;
        last = t;
        for (k = 0; k < 30; k++) {
            float e[3];
            if (G64EnemyInfo(k, &e[0], &e[1], &e[2])) {
                float j = sqrtf((e[0] - prev[k][0]) * (e[0] - prev[k][0]) + (e[1] - prev[k][1]) * (e[1] - prev[k][1]) +
                                (e[2] - prev[k][2]) * (e[2] - prev[k][2]));
                if (f > READY && j > r->maxJump)
                    r->maxJump = j;
            }
            memcpy(prev[k], e, sizeof e);
        }
    }
    where(0, r->end0);
    if (present == 0x3)
        where(1, r->end1);
    r->cp0 = G64PlayerCheckpoint(0);
    r->cp1 = present == 0x3 ? G64PlayerCheckpoint(1) : -2;
    r->lives0 = G64PlayerLives(0);
    r->lives1 = present == 0x3 ? G64PlayerLives(1) : -100;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    Run quiet, fall, own, solo;
    float g0[3], g1[3];
    char what[300];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (respawn ROM)\n");
        return 0;
    }
    fclose(rom);
    engine_default_options(&opt);
    engine = engine_create(argv[1], &opt, err, sizeof err);
    if (!engine) {
        printf("FAIL the engine didn't start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(engine));
    ready = malloc(engine_state_size(engine));
    if (!boot || !ready)
        return 1;
    engine_save(engine, boot);

    start(0x3, 1);
    if (!G64GatePosition(0, &g0[0], &g0[1], &g0[2]) || !G64GatePosition(1, &g1[0], &g1[1], &g1[2])) {
        check(0, "level 10 has two gates");
        return 1;
    }
    run(-1, -1, &quiet);
    run(1, -1, &fall);
    snprintf(what, sizeof what, "A: player 1's countdown starts at %d and runs %d steps of one frame; player 0's "
             "reaches %d", fall.firstCountdown, fall.steps, fall.otherMax);
    check(fall.firstCountdown >= 60 && fall.steps >= 60 && fall.otherMax == 0, what);
    snprintf(what, sizeof what, "A: player 1 comes back %.0f from the level's start; player 0 ends %.1f from where "
             "they end when nobody falls", dist2d(fall.end1, g0), dist2d(fall.end0, quiet.end0));
    check(dist2d(fall.end1, g0) < 60.0f && dist2d(fall.end0, quiet.end0) < 0.1f, what);
    snprintf(what, sizeof what, "A: lives before %d and %d, after %d and %d", quiet.lives0, quiet.lives1,
             fall.lives0, fall.lives1);
    check(fall.lives0 == quiet.lives0 && fall.lives1 == quiet.lives1, what);
    printf("A: hash %08x\n", port_hash_ram());

    run(1, 1, &own);
    snprintf(what, sizeof what, "B: checkpoints after player 1's ball crosses gate 1: player 0 at gate %d, player 1 "
             "at gate %d; player 1 comes back %.0f from gate 1", own.cp0, own.cp1, dist2d(own.end1, g1));
    check(own.cp0 == 0 && own.cp1 == 1 && dist2d(own.end1, g1) < 60.0f && own.firstCountdown >= 60, what);
    printf("B: hash %08x\n", port_hash_ram());

    {
        unsigned f;
        float over, under;
        engine_load(engine, ready);
        for (f = READY; f < READY + 40; f++) {
            G64PlacePlayer(1, g1[0], g1[1] + 20.0f, g1[2]);
            frame(f);
        }
        over = G64PlayerAltitude(1);
        for (; f < READY + 80; f++) {
            G64PlacePlayer(1, g1[0], g1[1] - 3000.0f, g1[2]);
            frame(f);
        }
        under = G64PlayerAltitude(1);
        snprintf(what, sizeof what, "D: player 1's altitude at gate 1, %.0f from player 0: %.1f over the ground, %.1f "
                 "under the level", dist2d(g1, quiet.end0), over, under);
        check(over >= 0.0f && over < 60.0f && under == -1.0f, what);
        printf("D: hash %08x\n", port_hash_ram());
    }

    {
        G64Save sv;
        unsigned f;
        int at5 = -1, after = -1, steps = 0, last = 0;
        engine_load(engine, boot);
        g64Online = 1;
        present = 0x3;
        G64StartPlayers(0x3);
        memset(&sv, 0, sizeof sv);
        sv.levels = 1;
        G64SetStartRecord(0, &sv);
        port_game_start_level(LEVEL);
        for (f = 0; f < 200; f++) {
            int t;
            if (f == 5)
                present = 0x2;
            frame(f);
            t = G64RestartCountdown(f < 5 ? 0 : 1);
            if (f == 5)
                at5 = t;
            if (f > 5 && t > 0 && t == last - 1)
                steps++;
            last = t;
            if (f == 199)
                after = t;
        }
        snprintf(what, sizeof what, "E: player 0 leaves during the opening: player 1's countdown is %d then, runs %d "
                 "more steps, and is %d at frame 200", at5, steps, after);
        check(at5 > 30 && steps >= at5 - 2 && after == 0, what);
        printf("E: hash %08x\n", port_hash_ram());
    }

    start(0x1, 0);
    run(0, -1, &solo);
    snprintf(what, sizeof what, "C: the largest jump of an enemy in a frame: %.0f while player 1 respawns online, "
             "%.0f while one player respawns offline", fall.maxJump, solo.maxJump);
    check(solo.firstCountdown >= 60 && fall.maxJump < 50.0f && solo.maxJump >= 50.0f, what);
    printf("C: hash %08x\n", port_hash_ram());

    printf("%s each player respawns at their own checkpoint\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
