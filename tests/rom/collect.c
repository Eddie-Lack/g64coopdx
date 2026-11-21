/* Garibs and pick-ups with more than one player (weemitt's g64players.c, "Garibs and pick-ups"), online, with the
 * ROM. Each case builds a level from the state right after power-on, with two players, and runs it until the second
 * player's slot exists (frame 400).
 *   A. Player 1's glove is put on the garib farthest from player 0. The garib goes, and both players' counts rise by
 *      one: the count is the team's. With nobody there, nothing changes.
 *   B. Both players' gloves are put on one garib. It is counted once.
 *   C. The players take garibs in turn. Both counts equal the number of garibs that went.
 *   D. Player 1's glove is put on a pick-up. Player 1 gets the power-up for as long as player 0 gets it alone, player
 *      0 does not get it, and it runs out.
 *   E. A power-up with orbiters, held by player 1 for 1,500 frames. The orbiters are actors inside the blocks that a
 *      swap exchanges, and the game once crashed on them.
 *   F. Player 0 takes a garib alone, and player 1 comes into the level afterwards. Player 1's count is the team's.
 *   G. Two players in the first space boss's level (level 38) for 900 frames. The boss keeps its rotor in one of
 *      the glove's orbiters.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: collect ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
int G64PlayerGaribs(int p);
int G64PlayerPowerup(int p, int type);
int G64GaribPlace(int k, float *x, float *y, float *z);
int G64PickupPlace(int k, float *x, float *y, float *z);
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

/* The number of garibs left. */
static int garibs_left(void) {
    float x, y, z;
    int n = 0;
    while (G64GaribPlace(n, &x, &y, &z))
        n++;
    return n;
}

/* The garib farthest from player 0, and its distance. */
static int far_garib(float *at, float *distance) {
    float p[3] = {0, 0, 0}, g[3];
    int n, far = -1;
    *distance = 0;
    G64PlayerPosition(0, &p[0], &p[1], &p[2]);
    for (n = 0; G64GaribPlace(n, &g[0], &g[1], &g[2]); n++) {
        float d = sqrtf((g[0] - p[0]) * (g[0] - p[0]) + (g[1] - p[1]) * (g[1] - p[1]) + (g[2] - p[2]) * (g[2] - p[2]));
        if (d > *distance) {
            *distance = d;
            far = n;
            memcpy(at, g, sizeof g);
        }
    }
    return far;
}

/* 30 frames from the ready state with these players' gloves (a bit mask) put on the spot. */
static void visit(unsigned who, const float *at) {
    unsigned f;
    engine_load(engine, ready);
    frame(READY, 0x3);
    if (who & 1)
        G64PlacePlayer(0, at[0], at[1], at[2]);
    if (who & 2)
        G64PlacePlayer(1, at[0], at[1], at[2]);
    for (f = 1; f < 30; f++)
        frame(READY + f, 0x3);
}

static void case_a(int level) {
    float at[3], d;
    int n, left, c0, c1, idle0, idle1, idleLeft;
    char what[240];
    start(level);
    n = garibs_left();
    far_garib(at, &d);
    visit(0, at);
    idle0 = G64PlayerGaribs(0), idle1 = G64PlayerGaribs(1), idleLeft = garibs_left();
    visit(2, at);
    c0 = G64PlayerGaribs(0), c1 = G64PlayerGaribs(1), left = garibs_left();
    snprintf(what, sizeof what, "A: level %d, %d garibs: player 1 on the one %.0f from player 0: counts %d and %d, %d "
             "left; with nobody there: counts %d and %d, %d left", level, n, d, c0, c1, left, idle0, idle1, idleLeft);
    check(d > 800.0f && c0 == 1 && c1 == 1 && left == n - 1 && idle0 == 0 && idle1 == 0 && idleLeft == n, what);
    printf("A: hash %08x\n", port_hash_ram());
}

static void case_b(int level) {
    float at[3], d;
    int n, left, c0, c1;
    char what[200];
    start(level);
    n = garibs_left();
    far_garib(at, &d);
    visit(3, at);
    c0 = G64PlayerGaribs(0), c1 = G64PlayerGaribs(1), left = garibs_left();
    snprintf(what, sizeof what, "B: level %d, %d garibs: both players on one garib: counts %d and %d, %d left", level,
             n, c0, c1, left);
    check(c0 == 1 && c1 == 1 && left == n - 1, what);
    printf("B: hash %08x\n", port_hash_ram());
}

static void case_c(int level) {
    float at[3];
    int n, left, c0, c1, turn;
    unsigned f = READY;
    char what[200];
    start(level);
    n = garibs_left();
    for (turn = 0; turn < 8 && G64GaribPlace(0, &at[0], &at[1], &at[2]); turn++) {
        int g;
        frame(f++, 0x3);
        G64PlacePlayer(turn & 1, at[0], at[1], at[2]);
        for (g = 0; g < 20; g++)
            frame(f++, 0x3);
    }
    c0 = G64PlayerGaribs(0), c1 = G64PlayerGaribs(1), left = garibs_left();
    snprintf(what, sizeof what, "C: level %d, %d garibs: the players take garibs in turn, %d times: counts %d and %d, "
             "%d left", level, n, turn, c0, c1, left);
    check(c0 == c1 && c0 == n - left && c0 >= 8, what);
    printf("C: hash %08x\n", port_hash_ram());
}

/* From the ready state, player `who` is put on pick-up k. Returns the frames for which they have its power-up, out
   of `frames`; *other counts the frames in which the other player has it. leave: the frame at which the glove is
   taken off the pick-up, which comes back after a time. */
static int pickup(int k, int who, int frames, int leave, int *other, int *type) {
    float at[3];
    int f, held = 0;
    engine_load(engine, ready);
    *type = G64PickupPlace(k, &at[0], &at[1], &at[2]) - 100;
    *other = 0;
    frame(READY, 0x3);
    G64PlacePlayer(who, at[0], at[1], at[2]);
    for (f = 1; f < frames; f++) {
        frame(READY + (unsigned)f, 0x3);
        if (f == leave)
            G64PlacePlayer(who, at[0] + 400.0f, at[1] + 100.0f, at[2]);
        held += G64PlayerPowerup(who, *type) > 0;
        *other += G64PlayerPowerup(!who, *type) > 0;
    }
    return held;
}

static void case_d(int level, int k, int frames, int leave) {
    int alone, second, other0, other1, type, end;
    char what[240];
    start(level);
    alone = pickup(k, 0, frames, leave, &other0, &type);
    second = pickup(k, 1, frames, leave, &other1, &type);
    end = G64PlayerPowerup(1, type);
    snprintf(what, sizeof what, "D: level %d, pick-up %d (type %d): player 0 alone has it for %d frames; player 1 has "
             "it for %d frames, player 0 for %d then, and player 1 has %d left at the end", level, k, type, alone,
             second, other1, end);
    check(type >= 0 && alone > 100 && second == alone && other0 == 0 && other1 == 0 && end == 0, what);
    printf("D: hash %08x\n", port_hash_ram());
}

static void case_e(int level, int k) {
    float at[3];
    int type, f, held = 0;
    char what[160];
    start(level);
    type = G64PickupPlace(k, &at[0], &at[1], &at[2]) - 100;
    frame(READY, 0x3);
    G64PlacePlayer(1, at[0], at[1], at[2]);
    for (f = 1; f < 1500; f++) {
        frame(READY + (unsigned)f, 0x3);
        held += G64PlayerPowerup(1, type) > 0;
    }
    snprintf(what, sizeof what, "E: level %d, pick-up %d (type %d): player 1 stays on it for 1500 frames and has it "
             "for %d", level, k, type, held);
    check(held > 1000, what);
    printf("E: hash %08x\n", port_hash_ram());
}

static void case_f(int level) {
    float at[3];
    G64Save s;
    unsigned f;
    int before, c0, c1;
    char what[200];
    engine_load(engine, boot);
    g64Online = 1;
    G64StartPlayers(0x1);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
    for (f = 0; f < READY; f++)
        frame(f, 0x1);
    G64GaribPlace(0, &at[0], &at[1], &at[2]);
    G64PlacePlayer(0, at[0], at[1], at[2]);
    for (; f < READY + 30; f++)
        frame(f, 0x1);
    before = G64PlayerGaribs(0);
    for (; f < READY + 130; f++)
        frame(f, 0x3);
    c0 = G64PlayerGaribs(0), c1 = G64PlayerGaribs(1);
    snprintf(what, sizeof what, "F: level %d: player 0 alone has %d, then player 1 comes: counts %d and %d", level,
             before, c0, c1);
    check(before >= 1 && c0 == before && c1 == c0, what);
    printf("F: hash %08x\n", port_hash_ram());
}

static void case_g(void) {
    unsigned f;
    start(38);
    for (f = READY; f < READY + 900; f++)
        frame(f, 0x3);
    check(G64PlayerGaribs(1) >= 0, "G: two players in the first space boss's level for 900 frames");
    printf("G: hash %08x\n", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (collect ROM)\n");
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
    case_a(10);
    case_a(26);
    case_b(20);
    case_c(15);
    case_d(16, 0, 1200, 40);
    case_d(36, 0, 1200, 80);
    case_d(27, 1, 1200, 80);
    case_e(11, 0);
    case_f(10);
    case_g();
    printf("%s every player takes garibs and pick-ups\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
