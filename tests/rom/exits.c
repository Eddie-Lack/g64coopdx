/* Exits in the online game (weemitt/src/game/g64players.c), with the ROM.
 *
 * Two players play the first hub section with g64Online on. Player 1's glove is put on a world door: player 1 must
 * take it alone, with a record of where it leads (the way room of that world), and leave the game at the next frame
 * while player 0 stays. Then a level-wide exit (ExitLevel) takes player 0 to the hub; as the last player, player 0
 * stays in the game but takes no other exit. make twoplayers runs it on Linux and under Wine and compares the outputs.
 *
 * Usage: exits ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>

#include "../../weemitt/bruteforce/engine/engine.h"

typedef struct {
    unsigned levels, allGaribs;
    unsigned char hubTarget, hubLevel, worldComplete, pad;
} G64Save;
typedef struct {
    int player, type, complete, level, world;
    G64Save save;
} G64ExitRecord;
extern int g64Online;
extern int g64Players;
extern G64ExitRecord g64Exits[16];
extern int g64ExitCount;
int G64PlayerPosition(int p, float *x, float *y, float *z);
int G64HasPlayer(int p);
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
int G64FindExit(int type, float *x, float *y, float *z);
void G64PlacePlayer(int p, float x, float y, float z);
void ExitLevel(int type, int pause, int complete);
unsigned int port_hash_ram(void);

/* enum ExitType and enum Level (weemitt/include/enums.h). */
enum { ATLANTIS_EXIT = 6, SPACE_EXIT = 11, HUB_EXIT = 13, WAYROOM = 42 };

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    unsigned f;
    int door = 0, took = -1, type;
    float dx = 0, dy = 0, dz = 0;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (exits ROM)\n");
        return 0;
    }
    fclose(rom);
    g64Online = 1;
    G64StartPlayers(0x3);
    engine_default_options(&opt);
    opt.startLevel = 0;
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    for (f = 0; f < 1100; f++) {
        EngineInput in = {0, 0, 0};
        float x, y, z;
        /* A presses close the opening tip; the other players' slots come once its camera has gone. */
        if (f < 600 && f % 30 == 0)
            in.buttons = 0x8000;
        G64SetPresent(0x3);
        if (took >= 0 && f == (unsigned)took + 1) {
            check(!G64HasPlayer(1) && G64HasPlayer(0) && g64Players == 1,
                  "player 1 leaves the game at the next frame; player 0 stays");
        }
        if (f >= 650 && took < 0 && G64PlayerPosition(1, &x, &y, &z)) {
            if (!door) {
                for (type = ATLANTIS_EXIT; type <= SPACE_EXIT && !door; type++)
                    if (G64FindExit(type, &dx, &dy, &dz))
                        door = type;
                check(door != 0, "the hub has a world door");
                if (!door)
                    break;
            }
            G64PlacePlayer(1, dx, dy, dz);
        }
        engine_step(e, &in);
        if (g64ExitCount > 0 && took < 0) {
            took = (int)f;
            check(g64ExitCount == 1 && g64Exits[0].player == 1 && g64Exits[0].type == door, "player 1 alone takes the door");
            check(g64Exits[0].level == WAYROOM && g64Exits[0].world == door - ATLANTIS_EXIT + 1,
                  "the door leads to that world's way room");
        }
        if (f == 900) {
            ExitLevel(HUB_EXIT, 1, 0);
            check(g64ExitCount == 1 && g64Exits[0].player == 0 && g64Exits[0].world == 0,
                  "a level-wide exit takes player 0 to the hub");
            ExitLevel(HUB_EXIT, 1, 0);
            check(g64ExitCount == 1, "a player takes one exit only");
        }
        if (f == 902)
            check(G64HasPlayer(0) && g64Players == 1, "the last player stays in the game");
        if ((f + 1) % 50 == 0)
            printf("frame %u hash %08x\n", f + 1, port_hash_ram());
    }
    check(took >= 0, "an exit was taken");
    printf("%s players take exits alone\n", failed ? "FAIL" : "ok  ");
    engine_free(e);
    return failed;
}
