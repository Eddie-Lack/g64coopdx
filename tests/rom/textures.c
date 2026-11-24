/* Each player's own ball texture (weemitt's g64players.c, G64ActorTextures), online, with the ROM.
 * The game names the ball's texture in one word of the ball's display list, and writes it when the ball changes.
 * With one word for all, a player who changed their ball changed the look of every ball. Each player now has
 * words of their own.
 *   A. Two players. Player 1's ball changes into the next kind, as the R button makes it. Player 1's ball is
 *      another kind and has another texture than before. Player 0's ball keeps its kind and its texture.
 *   B. The same for player 0's ball: player 1's ball keeps its kind and its texture.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: textures ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
int G64PlayerTexture(int player, int k);
int G64PlayerBallType(int player);
void G64TestChangeBall(int player);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { READY = 400, A = 0x8000, LEVEL = 10 };

static Engine *engine;
static void *boot, *ready;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

static void frame(unsigned short b0, unsigned short b1) {
    EngineInput in;
    G64SetPresent(0x3);
    memset(g64Buttons, 0, sizeof g64Buttons);
    memset(g64StickX, 0, sizeof g64StickX);
    memset(g64StickY, 0, sizeof g64StickY);
    g64Buttons[0] = b0;
    g64Buttons[1] = b1;
    memset(&in, 0, sizeof in);
    in.buttons = b0;
    engine_step(engine, &in);
}

/* Player p's ball changes, and the game runs until the change has ended. */
static void run_case(int p, const char *name) {
    int kind[2], tex[2], kindAfter[2], texAfter[2], q, f, other = 1 - p;
    engine_load(engine, ready);
    for (q = 0; q < 2; q++) {
        kind[q] = G64PlayerBallType(q);
        tex[q] = G64PlayerTexture(q, 0);
    }
    G64TestChangeBall(p);
    for (f = 0; f < 150; f++)
        frame(0, 0);
    for (q = 0; q < 2; q++) {
        kindAfter[q] = G64PlayerBallType(q);
        texAfter[q] = G64PlayerTexture(q, 0);
    }
    check(tex[0] > 0 && tex[0] == tex[1] && kind[0] == kind[1], "both balls start as the same kind with one texture");
    check(kindAfter[p] != kind[p], "the player's ball is another kind");
    check(texAfter[p] != tex[p], "their ball has another texture");
    check(kindAfter[other] == kind[other], "the other player's ball keeps its kind");
    check(texAfter[other] == tex[other], "the other player's ball keeps its texture");
    printf("%s: kinds %d %d then %d %d, textures %s, hash %08x\n", name, kind[0], kind[1], kindAfter[0], kindAfter[1],
           texAfter[0] == texAfter[1] ? "equal" : "differ", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    G64Save s;
    unsigned f;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (textures ROM)\n");
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
        frame(f < 300 && f % 30 == 0 ? A : 0, f < 300 && f % 30 == 0 ? A : 0);
    engine_save(engine, ready);
    run_case(1, "A");
    run_case(0, "B");
    printf("%s each player's ball has its own texture\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
