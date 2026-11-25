/* Enemies with more than one player (weemitt's g64players.c, "Enemies and bullets"), online, with the ROM. Each case
 * builds a level from the state right after power-on, with two players, and runs it until the second player's slot
 * exists (frame 400).
 *   A. Three enemies that go for a glove near them. Player 1 stands next to the enemy and player 0 is far away. The
 *      enemy ends as close to player 1 as it does to player 0 when player 0 stands there alone, and much closer than
 *      when nobody is near.
 *   B. Two players in the game's ending (level 47) for 1,500 frames. The ending's code takes the first actor of the
 *      list for its own, and the second player's proxies once came first.
 *   C. A player next to a droid in level 37. The droid's code read a stored 32-bit pointer as a native one, which
 *      broke the 64-bit builds with one player too.
 *   D. An enemy that shoots (level 25, enemy 0) aims at player 1 next to it: its bullets come as close to player 1 as
 *      to player 0 alone next to it. With nobody next to it, they stay far from player 0, who is far away.
 *   E. Spank's two babies (level 23) pass one ball between them. Two players walk about for 6,000 frames; the babies
 *      take a ball, but never hold both players' balls at once.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: enemies ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
int G64EnemyInfo(int i, float *x, float *y, float *z);
int G64BulletPositions(float *xyz, int max);
int G64BallHolder(int p);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

/* The frame from which both players are in the game in every level used here. */
enum { READY = 400, RUN = 90 };

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

/* A level from the power-on state with players 0 and 1, run to READY. */
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
}

static float dist(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

static void where(int p, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64PlayerPosition(p, &at[0], &at[1], &at[2]);
}

static void enemy_at(int k, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64EnemyInfo(k, &at[0], &at[1], &at[2]);
}

/* RUN frames from the READY state. who: the player put next to enemy k (-1: nobody); present: the players in the
   game. Returns the enemy's distance to the spot's player at the end; at gets that player's position. */
static float run(int k, int who, unsigned present, float *at) {
    float e[3], p[3];
    unsigned g;
    engine_load(engine, ready);
    enemy_at(k, e);
    frame(READY, present);
    if (who >= 0)
        G64PlacePlayer(who, e[0] + 120.0f, e[1] + 20.0f, e[2]);
    for (g = 1; g < RUN; g++)
        frame(READY + g, present);
    if (who >= 0) {
        where(who, p);
        memcpy(at, p, sizeof p);
    }
    enemy_at(k, e);
    return dist(e, at);
}

/* The distance from player p to the nearest bullet; 1e9 when there is none. */
static float nearest_bullet(int p) {
    float b[64 * 3], at[3], best = 1e9f;
    int n = G64BulletPositions(b, 64), i;
    where(p, at);
    for (i = 0; i < n && i < 64; i++) {
        float d = dist(&b[i * 3], at);
        if (d < best)
            best = d;
    }
    return best;
}

/* Like run, over 150 frames with the spot 250 from the enemy; returns how close a bullet came to the spot's player
   (player 0 when nobody is at the spot). */
static float shots(int k, int who, unsigned present) {
    float e[3], best = 1e9f;
    unsigned g;
    engine_load(engine, ready);
    enemy_at(k, e);
    frame(READY, present);
    if (who >= 0)
        G64PlacePlayer(who, e[0] + 250.0f, e[1] + 20.0f, e[2]);
    for (g = 1; g < 150; g++) {
        float d;
        frame(READY + g, present);
        d = nearest_bullet(who >= 0 ? who : 0);
        if (d < best)
            best = d;
    }
    return best;
}

static void case_a(int level, int k) {
    float spot[3], near, far, second, start0[3], e[3];
    char what[240];
    start(level);
    engine_save(engine, ready);
    where(0, start0);
    enemy_at(k, e);
    near = run(k, 0, 0x1, spot);
    far = run(k, -1, 0x1, spot);
    second = run(k, 1, 0x3, spot);
    snprintf(what, sizeof what, "A: level %d, enemy %d (player 0 %.0f away): ends %.0f from player 0 alone next to it, "
             "%.0f from player 1 next to it, %.0f from that spot with nobody there", level, k, dist(start0, e), near,
             second, far);
    check(dist(start0, e) > 800.0f && fabsf(second - near) < 1.0f && far - near > 50.0f, what);
    printf("A: hash %08x\n", port_hash_ram());
}

static void case_b(void) {
    unsigned f;
    char what[120];
    start(47);
    for (f = READY; f < READY + 1500; f++)
        frame(f, 0x3);
    snprintf(what, sizeof what, "B: two players in the ending for %u frames", f - READY);
    check(1, what);
    printf("B: hash %08x\n", port_hash_ram());
}

static void case_c(void) {
    float spot[3], near;
    char what[160];
    start(37);
    engine_save(engine, ready);
    near = run(5, 0, 0x1, spot);
    snprintf(what, sizeof what, "C: a glove next to the droid (level 37, enemy 5): the droid ends %.0f from it", near);
    check(near < 110.0f, what);
    printf("C: hash %08x\n", port_hash_ram());
}

static void case_d(void) {
    float near, far, second;
    char what[200];
    start(25);
    engine_save(engine, ready);
    near = shots(0, 0, 0x1);
    far = shots(0, -1, 0x1);
    second = shots(0, 1, 0x3);
    snprintf(what, sizeof what, "D: level 25, enemy 0: its bullets come %.0f from player 0 alone next to it, %.0f from "
             "player 1 next to it, and %.0f from player 0 far away", near, second, far);
    check(fabsf(second - near) < 50.0f && far - near > 500.0f, what);
    printf("D: hash %08x\n", port_hash_ram());
}

static void case_e(void) {
    static const signed char dirs[4][2] = {{0, 60}, {60, 0}, {0, -60}, {-60, 0}};
    unsigned f, held = 0, both = 0;
    char what[160];
    start(23);
    for (f = READY; f < READY + 6000; f++) {
        EngineInput in;
        int p, h0, h1;
        G64SetPresent(0x3);
        for (p = 0; p < 16; p++) {
            g64Buttons[p] = 0;
            g64StickX[p] = dirs[(f / (40 + 13 * (unsigned)p) + (unsigned)p) % 4][0];
            g64StickY[p] = dirs[(f / (40 + 13 * (unsigned)p) + (unsigned)p) % 4][1];
        }
        memset(&in, 0, sizeof in);
        in.x = g64StickX[0];
        in.y = g64StickY[0];
        engine_step(engine, &in);
        h0 = G64BallHolder(0);
        h1 = G64BallHolder(1);
        held += h0 >= 0 || h1 >= 0;
        both += h0 >= 0 && h1 >= 0;
    }
    snprintf(what, sizeof what, "E: level 23: a baby holds a ball in %u frames, both balls are held in %u", held, both);
    check(held > 300 && both == 0, what);
    printf("E: hash %08x\n", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (enemies ROM)\n");
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
    case_a(3, 5);
    case_a(3, 10);
    case_a(4, 2);
    case_b();
    case_c();
    case_d();
    case_e();
    printf("%s enemies go for the nearest player\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
