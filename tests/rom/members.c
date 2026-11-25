/* Players who join and leave a running game, by player number (weemitt/src/game/g64players.c), with the ROM.
 *
 * Players 0, 1 and 2 start in the first hub level. The players present come as a bit per player before each frame
 * (G64SetPresent), as the confirmed inputs give them. At frame 600 player 0, who's in the game's globals, leaves:
 * player 1 takes the globals. At frame 900 player 3 joins; at frame 1200 player 2 leaves. At frame 1300 player 3
 * leaves the way an exit takes a player (G64RemovePlayer) while their bit is still on, and must stay out. Each player
 * walks with their own input. The test checks that a glove stays where it was when another player leaves, and that
 * the right players are in the game after each change. It prints the memory hash every 50 frames: make twoplayers
 * runs it on Linux and under Wine and compares the outputs.
 *
 * Usage: members ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>

#include "../../weemitt/bruteforce/engine/engine.h"

#define G64_MAX_PLAYERS 16
extern int g64Players;
extern int g64Member[G64_MAX_PLAYERS];
extern unsigned short g64Buttons[G64_MAX_PLAYERS];
extern signed char g64StickX[G64_MAX_PLAYERS];
extern signed char g64StickY[G64_MAX_PLAYERS];
int G64PlayerPosition(int p, float *x, float *y, float *z);
int G64HasPlayer(int p);
extern float handStart[3];
void G64RemovePlayer(int p);
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
unsigned int port_hash_ram(void);

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* A player's controller: walks in a direction that changes every 40 frames, and jumps now and then. */
static void controller(int p, unsigned f, unsigned short *buttons, signed char *x, signed char *y) {
    static const signed char dirs[8][2] = {{0, 70}, {70, 0}, {0, -70}, {-70, 0}, {50, 50}, {-50, 50}, {50, -50},
                                           {-50, -50}};
    unsigned d = (f / 40 + (unsigned)p * 3) % 8;
    *x = dirs[d][0];
    *y = dirs[d][1];
    *buttons = (f + (unsigned)p * 17) % 60 < 4 ? 0x8000 : 0;
}

/* 1 when player p's glove is where it was. */
static int same_place(int p, const float *before) {
    float x, y, z;
    return G64PlayerPosition(p, &x, &y, &z) && x == before[0] && y == before[1] && z == before[2];
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    unsigned f;
    int p;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (members ROM)\n");
        return 0;
    }
    fclose(rom);
    G64StartPlayers(0x7);
    engine_default_options(&opt);
    opt.startLevel = 0;
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    for (f = 0; f < 1500; f++) {
        EngineInput in;
        unsigned present = f < 600 ? 0x7 : f < 900 ? 0x6 : f < 1200 ? 0xE : f < 1400 ? 0xA : 0x2;
        if (f == 1300) {
            G64RemovePlayer(3);
            check(!G64HasPlayer(3) && g64Players == 1, "player 3 leaves by an exit");
        }
        if (f == 600 || f == 900 || f == 1200) {
            float one[3], other[3];
            int moved = f == 1200 ? 1 : 2;
            G64PlayerPosition(moved, &one[0], &one[1], &one[2]);
            if (f == 600) {
                G64PlayerPosition(1, &other[0], &other[1], &other[2]);
                G64SetPresent(present);
                check(!G64HasPlayer(0) && g64Member[0] == 1 && g64Players == 2, "player 0 leaves; player 1 takes the globals");
                check(same_place(1, other) && same_place(2, one), "the other gloves stay where they were");
            } else if (f == 900) {
                G64SetPresent(present);
                check(G64HasPlayer(3) && g64Players == 3, "player 3 joins");
                check(same_place(2, one), "a glove stays where it was when a player joins");
                check(same_place(3, handStart), "the new player starts at the level's start");
            } else {
                G64SetPresent(present);
                check(!G64HasPlayer(2) && g64Players == 2 && g64Member[0] == 1 && g64Member[1] == 3,
                      "player 2 leaves; players 1 and 3 remain");
                check(same_place(1, one), "player 1's glove stays where it was");
            }
        }
        G64SetPresent(present);
        if (f == 1350)
            check(!G64HasPlayer(3) && g64Players == 1, "player 3 stays out while their bit is still on");
        for (p = 0; p < G64_MAX_PLAYERS; p++)
            controller(p, f, &g64Buttons[p], &g64StickX[p], &g64StickY[p]);
        /* The player in the globals reads the engine's controller. */
        controller(g64Member[0], f, &in.buttons, &in.x, &in.y);
        engine_step(e, &in);
        if ((f + 1) % 50 == 0) {
            printf("frame %u hash %08x", f + 1, port_hash_ram());
            for (p = 0; p < 4; p++) {
                float x, y, z;
                if (G64PlayerPosition(p, &x, &y, &z))
                    printf(" p%d %.1f %.1f %.1f", p, x, y, z);
            }
            printf("\n");
        }
    }
    check(G64HasPlayer(1) && g64Players == 1 && g64Member[0] == 1, "player 1 is the last one in the game");
    printf("%s players join and leave a running game\n", failed ? "FAIL" : "ok  ");
    engine_free(e);
    return failed;
}
