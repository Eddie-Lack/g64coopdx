/* Every level with eight players who press buttons and push sticks at random (weemitt's g64players.c), online, with
 * the ROM. It is the closing test of the roadmap's phase 3: the whole game with 8 players.
 * Each of the 48 levels is built from the state right after power-on with 8 players and run for 1000 frames. From
 * frame 300 each player's stick and buttons come from a seeded generator, the same on every machine: they walk,
 * jump, slam, grab and throw wherever that takes them, and they die and respawn. For each level it prints how
 * many players have a glove at the end, how many took an exit (a bot can walk into one, and a bonus level ends
 * for each player alone) and a hash of the game's memory. The run must not stop, and every player must have a
 * glove or have left through an exit. The control room (level 39) and the scenes from level 43 on give the other
 * players no glove.
 * make bots runs it on Linux and under Wine and compares the outputs: the same inputs must give the same game on
 * both.
 *
 * Usage: bots ROM [PLAYERS [FRAMES [any]]]. The last word turns the host's "any ball" rule on. Without a ROM file
 * it prints that it was skipped and exits with 0. */
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
int G64BallOwner(int player);
void port_game_start_level(int level);
/* The exits that players took in the last frame (weemitt's g64players.h). */
typedef struct {
    int player, type, complete, level, world;
    G64Save save;
} G64ExitRecord;
extern G64ExitRecord g64Exits[16];
extern int g64ExitCount;
unsigned int port_hash_ram(void);

/* From PRESENT on the levels are scenes, not play: the present, the two fly-throughs, the intro and the ending. */
enum { LEVELS = 48, CONTROL_ROOM = 39, FIRST_SCENE = 43, QUIET = 300 };

/* The buttons a bot may press: A, B, Z, R and the four C buttons. Never Start. */
#define BOT_BUTTONS 0xE01Fu

static unsigned seed;

static unsigned next(void) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    Engine *engine;
    void *boot;
    int players = argc > 2 ? atoi(argv[2]) : 8;
    unsigned frames = argc > 3 ? (unsigned)atoi(argv[3]) : 1000;
    /* A fourth word turns the host's "any ball" rule on: a glove may take any free ball. */
    int anyBall = argc > 4;
    unsigned mask;
    int level, failed = 0;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (bots ROM)\n");
        return 0;
    }
    fclose(rom);
    if (players < 2 || players > 16)
        players = 8;
    mask = (1u << players) - 1;
    engine_default_options(&opt);
    engine = engine_create(argv[1], &opt, err, sizeof err);
    if (!engine) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(engine));
    if (!boot)
        return 1;
    engine_save(engine, boot);
    for (level = 0; level < LEVELS; level++) {
        G64Save s;
        unsigned f;
        int p, k, gloves = 0, left = 0, others = 0, wanted = level == CONTROL_ROOM || level >= FIRST_SCENE ? 1 : players;
        unsigned leftMask = 0;
        float x, y, z;
        /* Each bot holds its stick and buttons for a while, then picks again. */
        unsigned short buttons[16];
        signed char sx[16], sy[16];
        memset(buttons, 0, sizeof buttons);
        memset(sx, 0, sizeof sx);
        memset(sy, 0, sizeof sy);
        seed = 12345u + (unsigned)level;
        engine_load(engine, boot);
        g64Online = 1;
        g64AnyBall = anyBall;
        G64StartPlayers(mask);
        memset(&s, 0, sizeof s);
        s.levels = 1;
        G64SetStartRecord(0, &s);
        port_game_start_level(level);
        for (f = 0; f < frames; f++) {
            EngineInput in;
            G64SetPresent(mask);
            for (p = 0; p < 16; p++) {
                if (p >= players) {
                    g64Buttons[p] = 0;
                    g64StickX[p] = g64StickY[p] = 0;
                    continue;
                }
                if (f < QUIET) {
                    /* A now and then: it closes the tip that the first hub opens by itself. */
                    buttons[p] = f % 30 == 0 ? 0x8000 : 0;
                } else if (next() % 12 == 0) {
                    sx[p] = (signed char)((int)(next() % 161) - 80);
                    sy[p] = (signed char)((int)(next() % 161) - 80);
                    buttons[p] = next() % 3 == 0 ? (unsigned short)(next() & BOT_BUTTONS) : 0;
                }
                g64Buttons[p] = buttons[p];
                g64StickX[p] = sx[p];
                g64StickY[p] = sy[p];
            }
            memset(&in, 0, sizeof in);
            in.buttons = g64Buttons[0];
            in.x = g64StickX[0];
            in.y = g64StickY[0];
            engine_step(engine, &in);
            for (k = 0; k < g64ExitCount && k < 16; k++)
                leftMask |= 1u << (g64Exits[k].player & 15);
        }
        for (p = 0; p < players; p++) {
            gloves += G64PlayerPosition(p, &x, &y, &z);
            left += (leftMask >> p) & 1;
        }
        /* Under the "any ball" rule: how many players have another player's ball at the end. */
        for (p = 0; p < players; p++)
            others += G64BallOwner(p) >= 0 && G64BallOwner(p) != p;
        printf("%s level %d: %d of %d players have a glove, %d took an exit, %d have another player's ball, hash %08x\n",
               gloves + left >= wanted ? "ok  " : "FAIL", level, gloves, players, left, others, port_hash_ram());
        fflush(stdout);
        failed |= gloves + left < wanted;
    }
    printf("%s every level runs with %d players%s\n", failed ? "FAIL" : "ok  ", players,
           anyBall ? " and the any-ball rule" : "");
    free(boot);
    engine_free(engine);
    return failed;
}
