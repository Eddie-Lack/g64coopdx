/* Bosses with more than one player (weemitt's boss.c), online, with the ROM. Each case builds a level from the state
 * right after power-on, with two players, and runs it until the second player's slot exists (frame 400).
 *   A. The Pirates boss's arena has a wall, a circle of radius 1100. It holds every player's glove and ball.
 *   B. In the Prehistoric boss's entry area the camera stands at a fixed place, for each player.
 *   C. The Prehistoric boss answers a ball that hits it: that ball comes back above its own glove. The other
 *      player's ball stays where it is.
 *   D. In the Carnival boss's level a ball that hits a trap's platform sets the trap off: another platform rises.
 *      Each player's camera circles the arena from where their glove is.
 * It prints what it checks and a memory hash per level: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: bosses ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
int G64EnemyInfo(int i, float *x, float *y, float *z);
int G64SoundListener(int p, float *pos, float *yaw, int *maxDist);
void G64PushBall(int p, float x, float y, float z);
int G64PlatformPosition(int tag, float *x, float *y, float *z);
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

static void glove(int p, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64PlayerPosition(p, &at[0], &at[1], &at[2]);
}

static void ball_at(int p, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64BallPosition(p, &at[0], &at[1], &at[2]);
}

static float dist(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

/* A: from the ready state of the Pirates boss's level, player `who`'s glove and ball are put outside the arena's
   wall (a circle of radius 1100). Returns how far from the middle the glove is 3 frames later; *b gets the ball's. */
static float outside(int who, float *b) {
    float at[3];
    unsigned f;
    engine_load(engine, ready);
    frame(READY, 0x3);
    glove(who, at);
    G64PlacePlayer(who, 1500.0f, at[1], 900.0f);
    G64PlaceBall(who, -1400.0f, at[1], 1000.0f);
    for (f = 1; f < 4; f++)
        frame(READY + f, 0x3);
    ball_at(who, at);
    *b = sqrtf(at[0] * at[0] + at[2] * at[2]);
    glove(who, at);
    return sqrtf(at[0] * at[0] + at[2] * at[2]);
}

static void case_a(void) {
    float g0, b0, g1, b1;
    char what[200];
    start(23);
    g0 = outside(0, &b0);
    g1 = outside(1, &b1);
    snprintf(what, sizeof what, "A: the Pirates boss's wall: put outside it, player 0's glove and ball end %.0f and "
             "%.0f from the middle, player 1's %.0f and %.0f", g0, b0, g1, b1);
    check(g0 < 1101.0f && b0 < 1101.0f && g1 < 1101.0f && b1 < 1101.0f, what);
    printf("A: hash %08x\n", port_hash_ram());
}

/* B: from the ready state of the Prehistoric boss's level, player `who`'s glove is put in the entry area. Returns
   how far their camera is from the area's fixed camera place 2 frames later. */
static float entry_camera(int who) {
    static const float place[3] = {-250.0f, 260.0f, -309.0f};
    float cam[3] = {0, 0, 0}, yaw;
    int max;
    unsigned f;
    engine_load(engine, ready);
    frame(READY, 0x3);
    G64PlacePlayer(who, 0.0f, -100.0f, -300.0f);
    for (f = 1; f < 3; f++) {
        frame(READY + f, 0x3);
        G64PlacePlayer(who, 0.0f, -100.0f, -300.0f);
    }
    G64SoundListener(who, cam, &yaw, &max);
    return dist(cam, place);
}

/* C: player `who`'s ball is put on the boss. Returns how near the ball comes to the place 100 above its glove,
   where the boss's answer to a hit puts it, in 12 frames; *other gets the same for the other player's ball. */
static float hit(int who, float *other) {
    float boss[3] = {0, 0, 0}, g[3], b[3], best = 1e9f;
    unsigned f;
    engine_load(engine, ready);
    *other = 1e9f;
    frame(READY, 0x3);
    G64EnemyInfo(0, &boss[0], &boss[1], &boss[2]);
    G64PlaceBall(who, boss[0], boss[1], boss[2]);
    for (f = 1; f < 13; f++) {
        float d;
        frame(READY + f, 0x3);
        glove(who, g);
        ball_at(who, b);
        g[1] += 100.0f;
        d = dist(g, b);
        if (d < best)
            best = d;
        glove(!who, g);
        ball_at(!who, b);
        g[1] += 100.0f;
        d = dist(g, b);
        if (d < *other)
            *other = d;
    }
    return best;
}

static void case_bc(void) {
    float c0, c1, h0, h1, o0, o1;
    char what[260];
    unsigned f;
    start(28);
    /* The level's opening lasts longer than READY. */
    for (f = READY; f < READY + 300; f++)
        frame(f, 0x3);
    engine_save(engine, ready);
    c0 = entry_camera(0);
    c1 = entry_camera(1);
    snprintf(what, sizeof what, "B: the Prehistoric boss's entry area: player 0's camera is %.0f from its fixed place, "
             "player 1's %.0f", c0, c1);
    check(c0 < 1.0f && c1 < 1.0f, what);
    h0 = hit(0, &o0);
    h1 = hit(1, &o1);
    snprintf(what, sizeof what, "C: a ball on the boss comes back above its glove: player 0's ball to %.0f of that "
             "place (player 1's ball %.0f), player 1's ball to %.0f (player 0's ball %.0f)", h0, o0, h1, o1);
    check(h0 < 30.0f && h1 < 30.0f && o0 > 60.0f && o1 > 60.0f, what);
    printf("B: hash %08x\n", port_hash_ram());
}

/* D: from the ready state of the Carnival boss's level, player `who`'s ball is thrown onto the platform with tag 1,
   and their glove is then held beside it. Returns how far the platform with tag 4 rises in 30 frames; cam gets the
   player's camera place 150 frames on. */
static float trap(int who, float *cam) {
    float p1[3] = {0, 0, 0}, p4[3] = {0, 0, 0}, yaw, low, high = -1e9f;
    int max;
    unsigned f;
    engine_load(engine, ready);
    G64PlatformPosition(1, &p1[0], &p1[1], &p1[2]);
    G64PlatformPosition(4, &p4[0], &p4[1], &p4[2]);
    low = p4[1];
    for (f = 0; f < 150; f++) {
        frame(READY + f, 0x3);
        if (f == 0) {
            G64PlaceBall(who, p1[0] * 0.8f, p1[1] + 40.0f, p1[2] * 0.8f);
            G64PushBall(who, p1[0] > 0 ? 6.0f : -6.0f, -2.0f, p1[2] > 0 ? 6.0f : -6.0f);
        }
        if (f >= 60)
            G64PlacePlayer(who, p1[0] * 1.2f, p1[1] + 80.0f, p1[2] * 1.2f);
        G64PlatformPosition(4, &p4[0], &p4[1], &p4[2]);
        if (f < 30 && p4[1] > high)
            high = p4[1];
    }
    cam[0] = cam[1] = cam[2] = 0;
    G64SoundListener(who, cam, &yaw, &max);
    return high - low;
}

static void case_d(void) {
    float rise0, rise1, cam0[3], cam1[3];
    char what[260];
    unsigned f;
    start(18);
    for (f = READY; f < READY + 300; f++)
        frame(f, 0x3);
    engine_save(engine, ready);
    rise0 = trap(0, cam0);
    rise1 = trap(1, cam1);
    snprintf(what, sizeof what, "D: the Carnival boss's trap: player 0's ball makes the platform rise %.0f, player 1's "
             "%.0f; their cameras end %.0f apart", rise0, rise1, dist(cam0, cam1));
    check(rise0 > 50.0f && rise1 > 50.0f && dist(cam0, cam1) < 15.0f, what);
    printf("D: hash %08x\n", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (bosses ROM)\n");
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
    case_a();
    case_bc();
    case_d();
    printf("%s the bosses see every player\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
