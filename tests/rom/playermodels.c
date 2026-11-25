/* Each player's chosen glove and ball models (weemitt's g64models.c), online, with the ROM. A model only changes what's
 * drawn in a player's view, and the client loads the state again after a view. So:
 *   A. The same inputs give the same memory hash every frame, with the game's own models and with other models, while
 *      both players' views are drawn every frame.
 *   B. The other models are drawn in a view, and not in the frame the game draws for itself.
 * It prints what it checks and the last hash: make twoplayers runs it on Linux and under Wine and compares the outputs.
 *
 * Usage: playermodels ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../weemitt/bruteforce/engine/engine.h"

typedef struct {
    unsigned levels, allGaribs;
    unsigned char hubTarget, hubLevel, worldComplete, raisedBit;
} G64Save;
typedef struct {
    unsigned int addr;
    unsigned int size;
    int sprite2d;
} PortFrameTask;
extern int g64Online;
extern unsigned short g64Buttons[16];
extern signed char g64StickX[16], g64StickY[16];
extern unsigned g64PlayerColor[16][2];
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
void G64SetStartRecord(int world, const G64Save *s);
void G64SwapPlayer(int player);
int G64TestModelDrawn(int player, int isBall);
int port_game_draw_view(PortFrameTask *tasks, int max);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

/* weemitt's g64models.h: a model's number is in bits 25 to 28 of a color. */
#define MODEL(n) ((unsigned)(n) << 25)

enum { READY = 300, FRAMES = 400, LEVEL = 0 };

static Engine *engine;
static void *ready, *view;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* Both players walk, each their own way, and jump now and then. */
static void frame(unsigned f) {
    static const signed char dirs[4][2] = {{0, 70}, {70, 0}, {0, -70}, {-70, 0}};
    EngineInput in;
    int p;
    G64SetPresent(0x3);
    for (p = 0; p < 2; p++) {
        unsigned d = (f / 40 + (unsigned)p) % 4;
        g64StickX[p] = dirs[d][0];
        g64StickY[p] = dirs[d][1];
        g64Buttons[p] = (f + (unsigned)p * 17) % 60 < 4 ? 0x8000 : 0;
    }
    memset(&in, 0, sizeof in);
    in.buttons = g64Buttons[0];
    in.x = g64StickX[0];
    in.y = g64StickY[0];
    engine_step(engine, &in);
}

/* Each player's view, as the client draws it: the state is loaded again after it. drawn gets 1 when a chosen model
 * was drawn in a view. */
static void views(int *drawn) {
    PortFrameTask tasks[16];
    int p;
    for (p = 0; p < 2; p++) {
        engine_save(engine, view);
        G64SwapPlayer(p);
        *drawn |= G64TestModelDrawn(1, 0) && G64TestModelDrawn(0, 1);
        port_game_draw_view(tasks, 16);
        engine_load(engine, view);
    }
}

/* A run from the ready state with these colors. hashes gets the hash after every frame. */
static void run(const unsigned colors[2][2], unsigned *hashes, int *drawnInView, int *drawnInGame) {
    unsigned f;
    engine_load(engine, ready);
    memcpy(g64PlayerColor, colors, 2 * sizeof colors[0]);
    *drawnInView = *drawnInGame = 0;
    for (f = 0; f < FRAMES; f++) {
        frame(READY + f);
        *drawnInGame |= G64TestModelDrawn(1, 0) || G64TestModelDrawn(0, 1);
        views(drawnInView);
        hashes[f] = port_hash_ram();
    }
}

int main(int argc, char **argv) {
    static const unsigned own[2][2] = {{0, 0}, {0x1000000u | 0xFFFFFF, 0x1000000u | 0x60A0FF}};
    /* Player 1's glove is Cross-Stitch (1); player 0's ball is the fish (2). Player 0's glove has a number past the
       list, which draws the game's own. */
    static const unsigned other[2][2] = {{MODEL(9), MODEL(2)}, {0x1000000u | 0xFFFFFF | MODEL(1), 0x1000000u | 0x60A0FF}};
    static unsigned a[FRAMES], b[FRAMES];
    EngineOptions opt;
    char err[256];
    G64Save s;
    unsigned f, differ = 0;
    int viewA, gameA, viewB, gameB;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (playermodels ROM)\n");
        return 0;
    }
    fclose(rom);
    engine_default_options(&opt);
    engine = engine_create(argv[1], &opt, err, sizeof err);
    if (!engine) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    ready = malloc(engine_state_size(engine));
    view = malloc(engine_state_size(engine));
    if (!ready || !view)
        return 1;
    g64Online = 1;
    G64StartPlayers(0x3);
    memset(&s, 0, sizeof s);
    G64SetStartRecord(0, &s);
    port_game_start_level(LEVEL);
    for (f = 0; f < READY; f++)
        frame(f);
    engine_save(engine, ready);
    run(own, a, &viewA, &gameA);
    run(other, b, &viewB, &gameB);
    for (f = 0; f < FRAMES; f++)
        differ += a[f] != b[f];
    check(differ == 0, "the hash is the same every frame with other models");
    check(!viewA && !gameA, "the game's own models are drawn when the players chose them");
    check(viewB, "the chosen models are drawn in the views");
    check(!gameB, "the frame the game draws for itself has the game's own models");
    printf("frames %u, differ %u, last hash %08x\n", FRAMES, differ, b[FRAMES - 1]);
    printf("%s each player's model changes only the picture\n", failed ? "FAIL" : "ok  ");
    free(view);
    free(ready);
    engine_free(engine);
    return failed;
}
