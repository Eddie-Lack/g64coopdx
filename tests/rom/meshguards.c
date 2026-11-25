/* The game with meshes that have no texture coordinates and no vertex colors, as a pack's meshes have none
 * (src/client/bankcheck.c), with the ROM.
 *
 * A level marks an object for moving texture coordinates with WATER or MODGE. DeformTextures then changes them on
 * every frame. The space boss's shield changes the vertex colors of the first foreground object. Both write through
 * the mesh's pointer. Without a test of that pointer, a pack object in such a place would crash the game.
 *   A. Atlantis 1. The test marks the first actor's object as MODGE does, and takes its texture coordinates away
 *      (G64TestDeformWithoutUV, weemitt's water.c). It runs the level on.
 *   B. The space boss's last part, after its opening (frame 700). The test takes the colors away from the first
 *      foreground object (G64TestDropShieldColors, weemitt's boss.c), and runs the level with Z held, which raises
 *      the shield.
 * Each case also passes the other way round: without the guards in DeformTextureCoords and DoSpaceBoss3, the game
 * crashes.
 * It prints what it checks and a memory hash per case: make meshguards runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: meshguards ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../weemitt/bruteforce/engine/engine.h"

int G64TestDeformWithoutUV(void);
int G64TestDropShieldColors(void);
extern int spaceBossShield;
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { ATLANTIS1 = 10, SPACEBOSS3 = 40, A = 0x8000, Z = 0x2000 };

static Engine *engine;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

static void run(unsigned frames, unsigned short buttons) {
    unsigned f;
    for (f = 0; f < frames; f++) {
        EngineInput in;
        memset(&in, 0, sizeof in);
        in.buttons = buttons;
        engine_step(engine, &in);
    }
}

static void start(const void *boot, int level) {
    unsigned f;
    engine_load(engine, boot);
    port_game_start_level(level);
    /* A presses through the level's start. */
    for (f = 0; f < 300; f += 30) {
        run(1, A);
        run(29, 0);
    }
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    void *boot;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (meshguards ROM)\n");
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
    if (!boot)
        return 1;
    engine_save(engine, boot);

    start(boot, ATLANTIS1);
    check(G64TestDeformWithoutUV() == 1, "A: the level has an actor drawn from a display list");
    run(300, 0);
    printf("A: a marked object without texture coordinates for 300 frames, hash %08x\n", port_hash_ram());

    start(boot, SPACEBOSS3);
    run(400, 0);
    check(G64TestDropShieldColors() == 1, "B: the level has a first foreground object");
    run(300, Z);
    check(spaceBossShield == 1, "B: the shield is up");
    printf("B: the shield up for 300 frames without colors, hash %08x\n", port_hash_ram());

    printf("%s the game runs with meshes that have no texture coordinates and no colors\n", failed ? "FAIL" : "ok  ");
    free(boot);
    engine_free(engine);
    return failed;
}
