/* Two players in one game (weemitt/src/game/g64players.c), with the ROM: both gloves play the first hub level with
 * different inputs. It prints both gloves' positions and the memory hash every 50 frames.
 *
 * The test: player 2's glove moves on its own (its position differs from player 1's), and the output, hashes
 * included, is the same on Linux and on Windows (make twoplayers compares them).
 *
 * Usage: twoplayers ROM [frames] [players]. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>

#include "../../weemitt/bruteforce/engine/engine.h"

/* The game's side (g64players.h uses the game's types). */
#define G64_MAX_PLAYERS 16
extern int g64Players;
extern unsigned short g64Buttons[G64_MAX_PLAYERS];
extern signed char g64StickX[G64_MAX_PLAYERS];
extern signed char g64StickY[G64_MAX_PLAYERS];
int G64PlayerPosition(int p, float *x, float *y, float *z);
unsigned int port_hash_ram(void);

/* A player's controller: walks in a direction that changes every 40 frames, and jumps now and then. */
static void controller(int p, unsigned f, unsigned short *buttons, signed char *x, signed char *y) {
    static const signed char dirs[8][2] = {{0, 70}, {70, 0}, {0, -70}, {-70, 0}, {50, 50}, {-50, 50}, {50, -50},
                                           {-50, -50}};
    unsigned d = (f / 40 + (unsigned)p * 3) % 8;
    *x = dirs[d][0];
    *y = dirs[d][1];
    *buttons = (f + (unsigned)p * 17) % 60 < 4 ? 0x8000 : 0;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    unsigned frames = argc > 2 ? (unsigned)atoi(argv[2]) : 600, f;
    int players = argc > 3 ? atoi(argv[3]) : 2, p, apart = 0;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (twoplayers ROM [frames] [players])\n");
        return 0;
    }
    fclose(rom);
    g64Players = players;
    engine_default_options(&opt);
    opt.startLevel = 0;
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    for (f = 0; f < frames; f++) {
        EngineInput in;
        for (p = 1; p < players && p < G64_MAX_PLAYERS; p++)
            controller(p, f, &g64Buttons[p], &g64StickX[p], &g64StickY[p]);
        controller(0, f, &in.buttons, &in.x, &in.y);
        engine_step(e, &in);
        if ((f + 1) % 50 == 0) {
            float x0, y0, z0;
            printf("frame %u hash %08x", f + 1, port_hash_ram());
            for (p = 0; p < players; p++) {
                float x, y, z;
                if (G64PlayerPosition(p, &x, &y, &z)) {
                    printf(" p%d %.1f %.1f %.1f", p, x, y, z);
                    if (p == 0) {
                        x0 = x, y0 = y, z0 = z;
                    } else if (x != x0 || y != y0 || z != z0) {
                        apart = 1;
                    }
                }
            }
            printf("\n");
        }
    }
    printf("%s the other gloves move on their own\n", players < 2 || apart ? "ok  " : "FAIL");
    engine_free(e);
    return players < 2 || apart ? 0 : 1;
}
