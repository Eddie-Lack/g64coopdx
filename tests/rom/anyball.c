/* The host's "any ball" rule (weemitt's g64players.c, "Any ball"), online, with the ROM.
 * With the rule a glove may take any free ball. The two players' balls then change places in the game: the glove's
 * player has the ball it touched, and the other player has the ball that was left. Every ball keeps its real
 * owner, and a ball that is lost kills its real owner, whoever has it.
 * Two players let go of their balls. Player 0's ball is put far from both gloves, and player 1's glove on it.
 *   A. Without the rule nothing changes: each player keeps their own ball.
 *   B. With the rule player 1 has player 0's ball, where it lay, and player 0 has player 1's.
 *   C. Then the ball that player 1 has is lost. Its real owner, player 0, dies: their respawn counts down. Player 1
 *      does not die, their glove stays where it is, and they get the ball back whole.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: anyball ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../weemitt/bruteforce/engine/engine.h"

typedef struct {
    unsigned levels, allGaribs;
    unsigned char hubTarget, hubLevel, worldComplete, raisedBit;
} G64Save;
extern int g64Online, g64AnyBall;
extern unsigned short g64Buttons[16];
extern signed char g64StickX[16], g64StickY[16];
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
void G64SetStartRecord(int world, const G64Save *s);
int G64PlayerPosition(int p, float *x, float *y, float *z);
int G64BallPosition(int p, float *x, float *y, float *z);
void G64PlacePlayer(int p, float x, float y, float z);
void G64PlaceBall(int p, float x, float y, float z);
int G64BallOwner(int player);
void G64TestLoseBall(int player);
int G64PlayerRestarting(int player);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { READY = 400, A = 0x8000, Z = 0x2000, LEVEL = 10 };

static Engine *engine;
static void *boot, *ready;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

static void frame(unsigned short b) {
    EngineInput in;
    G64SetPresent(0x3);
    memset(g64Buttons, 0, sizeof g64Buttons);
    memset(g64StickX, 0, sizeof g64StickX);
    memset(g64StickY, 0, sizeof g64StickY);
    g64Buttons[0] = g64Buttons[1] = b;
    memset(&in, 0, sizeof in);
    in.buttons = b;
    engine_step(engine, &in);
}

static float dist(const float *a, const float *b) {
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

static float spot[3], other[3], glove1[3];

/* Both let go of their balls. Player 0's ball goes to a spot away from both gloves, and player 1's glove onto it. */
static void set_up(int rule) {
    float g0[3], b0[3], b1[3];
    int f;
    engine_load(engine, ready);
    g64AnyBall = rule;
    for (f = 0; f < 60; f++)
        frame(f < 20 ? Z : 0);
    G64PlayerPosition(0, &g0[0], &g0[1], &g0[2]);
    G64BallPosition(1, &b1[0], &b1[1], &b1[2]);
    spot[0] = g0[0] + 120.0f;
    spot[1] = g0[1] + 10.0f;
    spot[2] = g0[2];
    G64PlaceBall(0, spot[0], spot[1], spot[2]);
    G64PlacePlayer(1, spot[0], spot[1], spot[2] + 12.0f);
    for (f = 0; f < 4; f++)
        frame(0);
    G64BallPosition(0, &b0[0], &b0[1], &b0[2]);
    memcpy(other, b1, sizeof other);
    G64PlayerPosition(1, &glove1[0], &glove1[1], &glove1[2]);
}

static void case_a(void) {
    float b0[3], b1[3];
    set_up(0);
    G64BallPosition(0, &b0[0], &b0[1], &b0[2]);
    G64BallPosition(1, &b1[0], &b1[1], &b1[2]);
    check(G64BallOwner(0) == 0 && G64BallOwner(1) == 1, "A: without the rule each player has their own ball");
    check(dist(b0, spot) < 40.0f, "A: player 0's ball is still player 0's, at the spot");
    printf("A: ball 0 is %.0f from the spot, hash %08x\n", dist(b0, spot), port_hash_ram());
}

static void case_b_c(void) {
    float b0[3], b1[3], g1[3], g1after[3];
    int f, before0, before1, dying0, dying1;
    set_up(1);
    G64BallPosition(0, &b0[0], &b0[1], &b0[2]);
    G64BallPosition(1, &b1[0], &b1[1], &b1[2]);
    check(G64BallOwner(1) == 0 && G64BallOwner(0) == 1, "B: with the rule player 1 has player 0's ball, and player 0 has player 1's");
    check(dist(b1, spot) < 40.0f, "B: player 1's ball is the one at the spot");
    check(dist(b0, other) < 40.0f, "B: player 0's ball is the one that player 1 left");
    printf("B: ball of player 1 is %.0f from the spot, ball of player 0 is %.0f from the other ball's place, hash %08x\n",
           dist(b1, spot), dist(b0, other), port_hash_ram());
    G64PlayerPosition(1, &g1[0], &g1[1], &g1[2]);
    before0 = G64PlayerRestarting(0);
    before1 = G64PlayerRestarting(1);
    G64TestLoseBall(1);
    for (f = 0; f < 5; f++)
        frame(0);
    dying0 = G64PlayerRestarting(0);
    dying1 = G64PlayerRestarting(1);
    check(G64BallOwner(1) == 0 && G64BallOwner(0) == 1, "C: the ball stays with the player who had it");
    check(before0 == 0 && before1 == 0 && dying0 > 0, "C: the ball's real owner died: their respawn counts down");
    check(dying1 == 0, "C: the player who had the ball did not die");
    for (f = 0; f < 200; f++)
        frame(0);
    G64PlayerPosition(1, &g1after[0], &g1after[1], &g1after[2]);
    check(dist(g1, g1after) < 40.0f, "C: their glove stays where it was");
    printf("C: respawn countdowns %d and %d, player 1 moved %.0f, hash %08x\n", dying0, dying1, dist(g1, g1after),
           port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    G64Save s;
    unsigned f;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (anyball ROM)\n");
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
    g64Online = 1;
    G64StartPlayers(0x3);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(LEVEL);
    for (f = 0; f < READY; f++)
        frame(f < 300 && f % 30 == 0 ? A : 0);
    engine_save(engine, ready);
    case_a();
    case_b_c();
    printf("%s a glove takes any free ball when the host allows it\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
