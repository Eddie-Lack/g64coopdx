/* Tips with more than one player (weemitt's tip.c and g64players.c, "Tips"), online, with the ROM.
 * Offline a tip stops the game while the player reads it. Online the level goes on: each player reads a tip on
 * their own, only the reader's glove is held, and the reader's A turns the page or closes the tip.
 *   A. A level with a tip, two players. Player 1 stands at the tip and presses B. Player 1 reads it and player 0
 *      does not. Player 0's glove still answers their stick, player 1's does not. Player 1 presses A until the
 *      tip closes, and their glove answers the stick again.
 *   B. Both players stand at the tip and press B: both read it. Player 0 closes theirs, and player 1 still reads.
 *   C. The first hub opens a tip by itself at frame 330. It opens for both players. With no A pressed the level
 *      goes on: the second player's glove is made when the opening scene ends, as without a tip. Player 1 closes
 *      theirs, and player 0 still reads.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: tips ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
int G64TipPlace(int k, float *x, float *y, float *z);
int G64TipPage(int player, const char **lines, int max, int *more);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { READY = 400, A = 0x8000, B = 0x4000, TIP_LEVEL = 10 };

static Engine *engine;
static void *boot, *ready, *asked;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* One frame. Each player has their own buttons and stick. */
static void frame(unsigned short b0, unsigned short b1, signed char stick0, signed char stick1) {
    EngineInput in;
    G64SetPresent(0x3);
    memset(g64Buttons, 0, sizeof g64Buttons);
    memset(g64StickX, 0, sizeof g64StickX);
    memset(g64StickY, 0, sizeof g64StickY);
    g64Buttons[0] = b0;
    g64Buttons[1] = b1;
    g64StickY[0] = stick0;
    g64StickY[1] = stick1;
    memset(&in, 0, sizeof in);
    in.buttons = b0;
    in.y = stick0;
    engine_step(engine, &in);
}

/* A level with two players, from the state right after power-on. A is pressed now and then for the first frames. */
static void start(int level, unsigned frames, unsigned presses) {
    G64Save s;
    unsigned f;
    engine_load(engine, boot);
    g64Online = 1;
    G64StartPlayers(0x3);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
    for (f = 0; f < frames; f++) {
        unsigned short b = f < presses && f % 30 == 0 ? A : 0;
        frame(b, b, 0, 0);
    }
}

static int reads(int p) {
    const char *lines[8];
    return G64TipPage(p, lines, 8, NULL) > 0;
}

/* How far the player's glove goes over the ground in n frames with their stick pushed. */
static float walk(int n, signed char stick0, signed char stick1, int p) {
    float x0, y0, z0, x1, y1, z1;
    int f;
    G64PlayerPosition(p, &x0, &y0, &z0);
    for (f = 0; f < n; f++)
        frame(0, 0, stick0, stick1);
    G64PlayerPosition(p, &x1, &y1, &z1);
    return sqrtf((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
}

/* The player presses A, with a frame's rest between presses, until their tip closes. It returns the presses. */
static int close_tip(int p) {
    int n;
    for (n = 0; n < 20 && reads(p); n++) {
        int f;
        for (f = 0; f < 15; f++)
            frame(0, 0, 0, 0);
        frame(p == 0 ? A : 0, p == 1 ? A : 0, 0, 0);
        frame(0, 0, 0, 0);
    }
    return n;
}

/* The players of the mask stand at the level's first tip and press B. */
static void ask(unsigned who) {
    float x, y, z;
    int p, f;
    if (!G64TipPlace(0, &x, &y, &z)) {
        check(0, "the level has a tip");
        return;
    }
    for (f = 0; f < 3; f++) {
        for (p = 0; p < 2; p++)
            if (who & (1u << p))
                G64PlacePlayer(p, x, y + 5.0f, z);
        frame(f == 1 && (who & 1) ? B : 0, f == 1 && (who & 2) ? B : 0, 0, 0);
    }
    frame(0, 0, 0, 0);
}

static void case_a(void) {
    float held, free0, after;
    int presses;
    engine_load(engine, ready);
    ask(0x2);
    check(reads(1), "A: the player at the tip who pressed B reads it");
    check(!reads(0), "A: the other player reads no tip");
    engine_save(engine, asked);
    free0 = walk(40, 70, 70, 0);
    engine_load(engine, asked);
    held = walk(40, 70, 70, 1);
    check(free0 > 20.0f, "A: the other player's glove answers their stick");
    check(held < 5.0f, "A: the reader's glove is held");
    check(reads(1), "A: the tip stays open while the level goes on");
    presses = close_tip(1);
    check(!reads(1) && presses >= 1 && presses < 20, "A: the reader's A turns the pages and closes the tip");
    after = walk(40, 0, 70, 1);
    check(after > 20.0f, "A: after the tip the reader's glove answers their stick");
    printf("A: other %.1f, held %.1f, %d presses, after %.1f, hash %08x\n", free0, held, presses, after,
           port_hash_ram());
}

static void case_b(void) {
    start(TIP_LEVEL, READY, 300);
    ask(0x3);
    check(reads(0) && reads(1), "B: both players at the tip read it");
    close_tip(0);
    check(!reads(0) && reads(1), "B: one player closes their tip, and the other still reads");
    printf("B: hash %08x\n", port_hash_ram());
}

static void case_c(void) {
    float x, y, z;
    int f, made;
    start(0, 329, 0);
    check(!reads(0) && !reads(1), "C: no tip is open before frame 330");
    for (f = 0; f < 6; f++)
        frame(0, 0, 0, 0);
    check(reads(0) && reads(1), "C: the first hub's own tip opens for both players");
    for (f = 0; f < 265; f++)
        frame(0, 0, 0, 0);
    made = G64PlayerPosition(1, &x, &y, &z);
    check(made, "C: the level goes on behind the tip: the second player's glove is made");
    close_tip(1);
    check(reads(0) && !reads(1), "C: one player closes the tip, and the other still reads");
    printf("C: hash %08x\n", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (tips ROM)\n");
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
    asked = malloc(engine_state_size(engine));
    if (!boot || !ready || !asked)
        return 1;
    engine_save(engine, boot);
    start(TIP_LEVEL, READY, 300);
    engine_save(engine, ready);
    case_a();
    case_b();
    case_c();
    printf("%s each player reads a tip on their own\n", failed ? "FAIL" : "ok  ");
    free(asked);
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
