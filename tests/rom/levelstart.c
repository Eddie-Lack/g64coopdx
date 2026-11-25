/* Levels built from the power-on state (weemitt's port_game_boot and port_game_start_level), with the ROM.
 *
 * g64coopdx builds every level the same way on every machine: it loads a state saved right after power-on, then
 * starts the level. A level reached by play isn't used: it keeps the camera of the level before, so its logic
 * drifts from a fresh start of the same level (docs/discovery/netcode.md, "Instances").
 *
 * For each level, the test starts it from the power-on state and runs it, then plays another level, goes back to
 * the power-on state and starts the first level again. Both runs must give the same memory hash at every check. It
 * prints one line per level; make levelstart also runs it under Wine and compares the outputs.
 *
 * Usage: levelstart ROM [frames]. Without a ROM file it prints that it was skipped and exits with 0. */
#include <stdio.h>
#include <stdlib.h>

#include "../../weemitt/bruteforce/engine/engine.h"

unsigned int port_hash_ram(void);
void port_game_start_level(int level);

/* The levels a player can be in (the hub sections, the cave, the assault course, the worlds, the way room). */
#define LEVELS 43

static void input(unsigned k, EngineInput *in) {
    static const signed char dirs[4][2] = {{0, 70}, {70, 0}, {0, -70}, {-70, 0}};
    in->x = dirs[(k / 30) % 4][0];
    in->y = dirs[(k / 30) % 4][1];
    in->buttons = k % 50 < 3 ? 0x8000 : 0;
}

/* Starts a level from the power-on state and runs it; returns a hash of its memory hashes every 50 frames. */
static unsigned run(Engine *e, const void *boot, int level, unsigned frames) {
    unsigned k, h = 2166136261u;
    engine_load(e, boot);
    port_game_start_level(level);
    for (k = 0; k < frames; k++) {
        EngineInput in;
        input(k, &in);
        engine_step(e, &in);
        if ((k + 1) % 50 == 0)
            h = (h ^ port_hash_ram()) * 16777619u;
    }
    return h;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    void *boot;
    unsigned frames = argc > 2 ? (unsigned)atoi(argv[2]) : 300;
    int level, failed = 0;
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (levelstart ROM [frames])\n");
        return 0;
    }
    fclose(rom);
    engine_default_options(&opt);
    e = engine_create(argv[1], &opt, err, sizeof err);
    if (!e) {
        printf("FAIL the engine did not start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(e));
    if (!boot) {
        printf("FAIL out of memory\n");
        return 1;
    }
    engine_save(e, boot);
    for (level = 0; level < LEVELS; level++) {
        int other = (level * 7 + 5) % LEVELS;
        unsigned first = run(e, boot, level, frames), again;
        run(e, boot, other, frames);
        again = run(e, boot, level, frames);
        printf("%s level %d: %08x, again after level %d: %08x\n", first == again ? "ok  " : "FAIL", level, first, other,
               again);
        failed |= first != again;
    }
    printf("%s every level starts the same from the power-on state\n", failed ? "FAIL" : "ok  ");
    engine_free(e);
    return failed;
}
