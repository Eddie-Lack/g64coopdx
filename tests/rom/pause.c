/* The choices of a player's own pause menu (G64PlayerAction, weemitt/src/game/g64players.c), with the ROM.
 *
 * Players 0 and 1 start in the first hub level. They press A until the opening tip closes, then walk with their own
 * input. At frame 800 player 1 goes back to their checkpoint; at frame 1000 player 0, who is in the game's globals,
 * does. Each time the other glove must stay where it was. At frame 1200 player 1 goes back to the hub: they leave the level the way an exit takes them, and player 0 stays.
 * It prints the memory hash every 100 frames: make twoplayers runs it on Linux and under Wine and compares the outputs.
 *
 * Usage: pause ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>

#include "../../weemitt/bruteforce/engine/engine.h"

#define G64_MAX_PLAYERS 16
#define ACTION_CHECKPOINT 1 /* G64_ACTION_CHECKPOINT and G64_ACTION_HUB, g64players.h */
#define ACTION_HUB 2
extern int g64Players;
extern int g64Member[G64_MAX_PLAYERS];
extern unsigned short g64Buttons[G64_MAX_PLAYERS];
extern signed char g64StickX[G64_MAX_PLAYERS];
extern signed char g64StickY[G64_MAX_PLAYERS];
int G64PlayerPosition(int p, float *x, float *y, float *z);
int G64BallPosition(int p, float *x, float *y, float *z);
int G64HasPlayer(int p);
void G64PlayerAction(int p, int action);
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
unsigned int port_hash_ram(void);

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* A player's controller: A presses until frame 600, then a walk in a direction that changes every 40 frames. */
static void controller(int p, unsigned f, unsigned short *buttons, signed char *x, signed char *y) {
    static const signed char dirs[4][2] = {{0, 70}, {70, 0}, {0, -70}, {-70, 0}};
    unsigned d = (f / 40 + (unsigned)p) % 4;
    *x = dirs[d][0];
    *y = dirs[d][1];
    *buttons = f < 600 && f % 30 == 0 ? 0x8000 : 0;
}

static void where(int p, float *at) {
    at[0] = at[1] = at[2] = 0.0f;
    G64PlayerPosition(p, &at[0], &at[1], &at[2]);
}

static void ball_at(int p, float *at) {
    at[0] = at[1] = at[2] = 0.0f;
    G64BallPosition(p, &at[0], &at[1], &at[2]);
}

/* 1 when a and b are closer than d. */
static int near(const float *a, const float *b, float d) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z < d * d;
}

static int same(const float *a, const float *b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; }

/* Player p goes back to their checkpoint; the other player must not move. */
static void back_to_checkpoint(int p) {
    float mine[3], mineAfter[3], other[3], otherAfter[3], ball[3], ballAfter[3];
    char what[120];
    where(p, mine);
    where(1 - p, other);
    ball_at(p, ball);
    G64PlayerAction(p, ACTION_CHECKPOINT);
    where(p, mineAfter);
    where(1 - p, otherAfter);
    ball_at(p, ballAfter);
    printf("player %d: %.1f %.1f %.1f to %.1f %.1f %.1f\n", p, mine[0], mine[1], mine[2], mineAfter[0], mineAfter[1],
           mineAfter[2]);
    snprintf(what, sizeof what, "player %d goes back to their checkpoint; player %d stays", p, 1 - p);
    check(!same(mine, mineAfter) && same(other, otherAfter), what);
    printf("player %d's ball: %.1f %.1f %.1f to %.1f %.1f %.1f\n", p, ball[0], ball[1], ball[2], ballAfter[0],
           ballAfter[1], ballAfter[2]);
    snprintf(what, sizeof what, "player %d's ball comes to the checkpoint too", p);
    check(near(ballAfter, mineAfter, 50.0f), what);
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    unsigned f;
    int p;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (pause ROM)\n");
        return 0;
    }
    fclose(rom);
    G64StartPlayers(0x3);
    engine_default_options(&opt);
    opt.startLevel = 0;
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    for (f = 0; f < 1400; f++) {
        EngineInput in;
        G64SetPresent(0x3);
        if (f == 800)
            back_to_checkpoint(1);
        if (f == 1000)
            back_to_checkpoint(0);
        if (f == 1200)
            G64PlayerAction(1, ACTION_HUB);
        /* The player leaves at the end of the frame, as after an exit. */
        if (f == 1201)
            check(!G64HasPlayer(1) && G64HasPlayer(0), "player 1 goes back to the hub; player 0 stays");
        /* A choice no menu gives does nothing. */
        if (f == 1250)
            G64PlayerAction(0, 77);
        for (p = 0; p < G64_MAX_PLAYERS; p++)
            controller(p, f, &g64Buttons[p], &g64StickX[p], &g64StickY[p]);
        controller(g64Member[0], f, &in.buttons, &in.x, &in.y);
        engine_step(e, &in);
        if ((f + 1) % 100 == 0)
            printf("frame %u hash %08x\n", f + 1, port_hash_ram());
    }
    check(G64HasPlayer(0) && !G64HasPlayer(1) && g64Players == 1, "player 0 is the last one in the level");
    printf("%s each player's pause menu acts for them alone\n", failed ? "FAIL" : "ok  ");
    engine_free(e);
    return failed;
}
