/* The look-around mode with more than one player (weemitt's controller.c and g64players.c), online, with the ROM.
 * A level is built from the state right after power-on, with two players, and run until the second player's slot
 * exists (frame 400).
 *   A. A player presses C-up and holds the stick to the left. The point their camera looks at swings round, by
 *      the same distance for player 0 and for player 1. The other player's glove still answers their own stick.
 *   B. Both players take up the look-around mode, facing different ways, and the first one leaves it. Their camera
 *      turns back to where it looked before, not to where the other player's looked.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: lookaround ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
int G64CameraTarget(int p, float *x, float *y, float *z);
int G64SoundListener(int p, float *pos, float *yaw, int *maxDist);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { READY = 400, C_UP = 0x0008, C_DOWN = 0x0004 };

static Engine *engine;
static void *boot, *ready;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* One frame. Each player has their own buttons and stick. */
static void frame(const unsigned short *buttons, const signed char *sx, const signed char *sy) {
    EngineInput in;
    int p;
    G64SetPresent(0x3);
    for (p = 0; p < 16; p++) {
        g64Buttons[p] = p < 2 ? buttons[p] : 0;
        g64StickX[p] = p < 2 ? sx[p] : 0;
        g64StickY[p] = p < 2 ? sy[p] : 0;
    }
    memset(&in, 0, sizeof in);
    in.buttons = g64Buttons[0];
    in.x = g64StickX[0];
    in.y = g64StickY[0];
    engine_step(engine, &in);
}

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
    for (f = 0; f < READY; f++) {
        unsigned short b[2];
        signed char z[2] = {0, 0};
        b[0] = b[1] = f < 300 && f % 30 == 0 ? 0x8000 : 0;
        frame(b, z, z);
    }
    engine_save(engine, ready);
}

static float dist(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

static void target(int p, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64CameraTarget(p, &at[0], &at[1], &at[2]);
}

static void glove(int p, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64PlayerPosition(p, &at[0], &at[1], &at[2]);
}

/* A: player `who` looks around to the left for 60 frames while the other player walks. Returns how far who's
   camera target swings; *walked gets how far the other player's glove moves. */
static float look(int who, float *walked) {
    float t0[3], t1[3], g0[3], g1[3];
    unsigned short b[2] = {0, 0};
    signed char sx[2] = {0, 0}, sy[2] = {0, 0};
    int f;
    engine_load(engine, ready);
    b[who] = C_UP;
    frame(b, sx, sy);
    b[who] = 0;
    for (f = 0; f < 30; f++)
        frame(b, sx, sy);
    target(who, t0);
    glove(!who, g0);
    sx[who] = -70;
    sy[!who] = 70;
    for (f = 0; f < 60; f++)
        frame(b, sx, sy);
    target(who, t1);
    glove(!who, g1);
    *walked = dist(g0, g1);
    return dist(t0, t1);
}

static void case_a(void) {
    float swing0, swing1, walk0, walk1;
    char what[240];
    swing0 = look(0, &walk1);
    swing1 = look(1, &walk0);
    snprintf(what, sizeof what, "A: the look-around mode swings player 0's camera target by %.0f and player 1's by "
             "%.0f; meanwhile the other player walks %.0f and %.0f", swing0, swing1, walk1, walk0);
    check(swing0 > 30.0f && fabsf(swing0 - swing1) < 15.0f && walk0 > 30.0f && walk1 > 30.0f, what);
    printf("A: hash %08x\n", port_hash_ram());
}

static float yaw_of(int p) {
    float pos[3], yaw = 0;
    int max;
    G64SoundListener(p, pos, &yaw, &max);
    return yaw;
}

static float turn(float a, float b) {
    float d = fabsf(a - b);
    while (d > 6.2831853f)
        d -= 6.2831853f;
    return d > 3.1415927f ? 6.2831853f - d : d;
}

static void case_b(void) {
    unsigned short b[2] = {0, 0};
    signed char sx[2] = {0, 0}, sy[2] = {0, 0};
    float before0, before1, after0;
    char what[240];
    int f;
    engine_load(engine, ready);
    /* Player 1 turns their camera a quarter round first, with the C buttons' turn (C-right). */
    b[1] = 0x0001;
    for (f = 0; f < 5; f++)
        frame(b, sx, sy);
    b[1] = 0;
    for (f = 0; f < 60; f++)
        frame(b, sx, sy);
    before0 = yaw_of(0);
    before1 = yaw_of(1);
    b[0] = C_UP;
    frame(b, sx, sy);
    b[0] = 0;
    for (f = 0; f < 30; f++)
        frame(b, sx, sy);
    b[1] = C_UP;
    frame(b, sx, sy);
    b[1] = 0;
    for (f = 0; f < 30; f++)
        frame(b, sx, sy);
    b[0] = C_DOWN;
    frame(b, sx, sy);
    b[0] = 0;
    for (f = 0; f < 90; f++)
        frame(b, sx, sy);
    after0 = yaw_of(0);
    snprintf(what, sizeof what, "B: the players' cameras look %.2f apart; after both take up the look-around mode "
             "and player 0 leaves it, player 0's camera is %.2f from where it looked", turn(before0, before1),
             turn(after0, before0));
    check(turn(before0, before1) > 0.5f && turn(after0, before0) < 0.2f, what);
    printf("B: hash %08x\n", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (lookaround ROM)\n");
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
    start(10);
    case_a();
    case_b();
    printf("%s each player has their own look-around mode\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
