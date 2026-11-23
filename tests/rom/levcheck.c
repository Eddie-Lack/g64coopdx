/* The level file check (src/client/levcheck.c) against the game's own level parser, with the ROM.
 *
 * For each of the game's 48 levels the test starts the level, which makes the game's parser read the level's file
 * and record every token with its offset (weemitt's G64ParseTrace). The check then walks the same file from the
 * ROM. It must accept the file and name the same tokens at the same offsets, the end token too. A difference
 * means the check reads a token's length otherwise than the game does.
 *
 * Usage: levcheck ROM. Without a ROM file it prints that it was skipped and exits with 0. make levcheck also runs
 * it under Wine and compares the outputs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/levcheck.h"
#include "../../src/client/pack.h"
#include "../../weemitt/bruteforce/engine/engine.h"

void port_game_start_level(int level);
extern int g64ParseLogOn, g64ParseLogCount;
extern int g64ParseLog[8192][2];

#define LEVELS 48

static struct {
    size_t offset;
    int token;
} seen[8192];
static int seenCount;

static void note(void *user, size_t offset, int token) {
    (void)user;
    if (seenCount < 8192) {
        seen[seenCount].offset = offset;
        seen[seenCount].token = token;
    }
    seenCount++;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    Engine *e;
    char err[256];
    void *boot;
    const uint8_t *rom;
    unsigned long romSize = 0;
    int level, failed = 0;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        printf("skipped: no ROM (levcheck ROM)\n");
        return 0;
    }
    fclose(f);
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
    rom = engine_rom(e, &romSize);
    for (level = 0; level < LEVELS; level++) {
        size_t at = 0, size = 0, bad = 0;
        const char *why;
        int k, same = 1;
        engine_load(e, boot);
        g64ParseLogOn = 1;
        g64ParseLogCount = 0;
        port_game_start_level(level);
        for (k = 0; k < 5; k++) {
            EngineInput in;
            memset(&in, 0, sizeof in);
            engine_step(e, &in);
        }
        why = g64_pack_level_from_rom(rom, romSize, level, &at, &size);
        if (why) {
            printf("FAIL level %d: %s\n", level, why);
            failed++;
            continue;
        }
        seenCount = 0;
        why = g64_lev_check(rom + at, size, note, NULL, &bad);
        if (why) {
            printf("FAIL level %d: the check refuses the game's own level at offset %lu: %s\n", level,
                   (unsigned long)bad, why);
            failed++;
            continue;
        }
        if (seenCount != g64ParseLogCount || seenCount > 8192)
            same = 0;
        for (k = 0; same && k < seenCount; k++)
            same = seen[k].offset == (size_t)g64ParseLog[k][0] && seen[k].token == g64ParseLog[k][1];
        if (!same) {
            for (k = 0; k < seenCount && k < g64ParseLogCount && k < 8192; k++)
                if (seen[k].offset != (size_t)g64ParseLog[k][0] || seen[k].token != g64ParseLog[k][1])
                    break;
            printf("FAIL level %d: the game read %d tokens and the check %d; they part at token %d\n", level,
                   g64ParseLogCount, seenCount, k);
            failed++;
            continue;
        }
        printf("ok   level %d: %d tokens, the same as the game's parser read\n", level, seenCount);
    }
    printf("%s the level file check reads every level of the ROM as the game does\n", failed ? "FAIL" : "ok  ");
    free(boot);
    engine_free(e);
    return failed != 0;
}
