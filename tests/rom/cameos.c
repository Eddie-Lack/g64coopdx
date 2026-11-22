/* A cameo that a player sets off (weemitt's cameo.c, puzzle.c and g64players.c, "Cameos"), online, with the ROM.
 * A cameo is a short scripted scene. Most play when a level starts. Three in the game start when a player meets a
 * puzzle's condition. One is in level 15: a glove and a ball on platform 203. A cameo plays for the player who set
 * it off: their glove and camera are the scene's, and the other players are held and watch.
 *   A. Two players. Player 1's glove and ball are put on the platform. The cameo starts, and it is player 1's.
 *      While it plays, player 0's glove does not answer their stick. When it has ended, no cameo has a player and
 *      player 0's glove answers again. While it plays only the cameo's player has a glove that is drawn.
 *   B. The same with player 0 on the platform: the cameo is player 0's.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: cameos ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
void G64PlaceBall(int p, float x, float y, float z);
int G64PlatformPosition(int tag, float *x, float *y, float *z);
int G64CameoPlayer(void);
int G64ProxyShown(int player);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { READY = 400, A = 0x8000, LEVEL = 15, PLATFORM = 203 };

static Engine *engine;
static void *boot, *ready;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

static void frame(unsigned short b, signed char stick0, signed char stick1) {
    EngineInput in;
    G64SetPresent(0x3);
    memset(g64Buttons, 0, sizeof g64Buttons);
    memset(g64StickX, 0, sizeof g64StickX);
    memset(g64StickY, 0, sizeof g64StickY);
    g64Buttons[0] = g64Buttons[1] = b;
    g64StickY[0] = stick0;
    g64StickY[1] = stick1;
    memset(&in, 0, sizeof in);
    in.buttons = b;
    in.y = stick0;
    engine_step(engine, &in);
}

static float walk(int n, int p) {
    float x0, y0, z0, x1, y1, z1;
    int f;
    G64PlayerPosition(p, &x0, &y0, &z0);
    for (f = 0; f < n; f++)
        frame(0, 70, 70);
    G64PlayerPosition(p, &x1, &y1, &z1);
    return sqrtf((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
}

/* Player p's glove and ball are put over the platform and fall on it. It returns the frames until a cameo plays,
   or -1. */
static int stand(int p) {
    float x, y, z;
    int f;
    if (!G64PlatformPosition(PLATFORM, &x, &y, &z))
        return -1;
    /* The platform's top is about 100 above its own point. */
    G64PlaceBall(p, x, y + 120.0f, z);
    G64PlacePlayer(p, x + 12.0f, y + 120.0f, z);
    for (f = 0; f < 100; f++) {
        frame(0, 0, 0);
        if (G64CameoPlayer() >= 0)
            return f;
    }
    return -1;
}

static void run_case(int p, const char *name) {
    float held, after;
    int started, f, owner, length, shownBefore, shownIn, shownAfter;
    engine_load(engine, ready);
    check(G64CameoPlayer() == -1, "no cameo plays before a player stands on the platform");
    started = stand(p);
    owner = G64CameoPlayer();
    check(started >= 0, "the cameo starts when a glove and a ball are on the platform");
    check(owner == p, "the cameo is the player's who set it off");
    /* Player 1's glove is drawn from a proxy. In a cameo only the cameo's own player is drawn. */
    shownBefore = G64ProxyShown(1);
    held = walk(10, 1 - p);
    shownIn = G64ProxyShown(1);
    for (f = 0; f < 1500 && G64CameoPlayer() >= 0; f++)
        frame(0, 0, 0);
    length = f;
    check(G64CameoPlayer() == -1, "the cameo ends");
    for (f = 0; f < 5; f++)
        frame(0, 0, 0);
    after = walk(40, 1 - p);
    /* This cameo ends the level for its player, who leaves: after it no player is drawn from a proxy. */
    shownAfter = G64ProxyShown(1);
    check(shownIn == (p == 1), "in the cameo only its player's glove is drawn");
    check(held < 3.0f, "the other player's glove is held while it plays");
    check(after > 20.0f, "the other player's glove answers their stick after it");
    printf("%s: player 1's glove drawn %d before, %d in, %d after the cameo\n", name, shownBefore, shownIn, shownAfter);
    printf("%s: started after %d frames, player %d, other held %.1f, after %.1f, ended after %d frames, hash %08x\n",
           name, started, owner, held, after, length, port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    G64Save s;
    unsigned f;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (cameos ROM)\n");
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
        frame(f < 300 && f % 30 == 0 ? A : 0, 0, 0);
    engine_save(engine, ready);
    run_case(1, "A");
    run_case(0, "B");
    printf("%s a cameo plays for the player who set it off\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
