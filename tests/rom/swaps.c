/* Every level with another level's file in its place, as a level pack from a host can do it (docs/discovery/
 * level-pack.md). The game's code for the hubs, the boss levels, the bonus levels and the scenes expects what their
 * own files hold. With another file the game must either play the level or show its error screen for a level that
 * can't be played (game mode 13). It must never stop, read outside its memory or run without end.
 *
 * For each pair of a level and another level's file, a child process builds the level from the state right after
 * power-on, with two players who press buttons and push sticks at random, and runs FRAMES frames. The parent
 * prints one line for each pair: "ok" when it played, "unplayable" when the game stopped it, and "FAIL" with the
 * signal or "hang" when the child crashed or didn't finish in time.
 *
 * Usage: swaps ROM [FRAMES [LEVEL]]. LEVEL limits the run to one level's place. Without a ROM file it prints that it
 * was skipped and exits with 0. Linux only: it needs fork. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../weemitt/bruteforce/engine/engine.h"
#include "../../src/client/pack.h"

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
void G64SetPack(int level, unsigned rom, unsigned size);
void port_game_start_level(int level);
int port_game_mode(void);
int port_game_level(void);

enum { LEVELS = 48, UNPLAYABLE = 13, QUIET = 100, SECONDS = 60 };

/* The buttons a bot may press: A, B, Z, R and the four C buttons. Never Start. */
#define BOT_BUTTONS 0xE01Fu

static unsigned seed;

static unsigned next(void) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

/* The child's run: level with the file of level donor. Exits with 0 when it played, 2 when the game stopped it. */
static int run(Engine *engine, void *boot, const uint8_t *rom, unsigned long romSize, int level, int donor,
               unsigned frames) {
    size_t at = 0, size = 0;
    uint8_t *with;
    G64Save s;
    unsigned f;
    int p;
    unsigned short buttons[2] = {0, 0};
    signed char sx[2] = {0, 0}, sy[2] = {0, 0};
    if (g64_pack_level_from_rom(rom, romSize, donor, &at, &size) != NULL)
        return 3;
    with = malloc(romSize + size);
    if (!with)
        return 3;
    memcpy(with, rom, romSize);
    memcpy(with + romSize, rom + at, size);
    engine_load(engine, boot);
    engine_set_rom(engine, with, romSize + size);
    G64SetPack(level, (unsigned)romSize, (unsigned)size);
    seed = 777u + (unsigned)(level * LEVELS + donor);
    g64Online = 1;
    G64StartPlayers(3u);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
    for (f = 0; f < frames; f++) {
        EngineInput in;
        G64SetPresent(3u);
        for (p = 0; p < 2; p++) {
            if (f < QUIET) {
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
    }
    if (getenv("SWAPS_DEBUG"))
        fprintf(stderr, "level %d donor %d: mode %d level %d size %lu\n", level, donor, port_game_mode(), port_game_level(), (unsigned long)size);
    return port_game_mode() == UNPLAYABLE ? 2 : 0;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    Engine *engine;
    void *boot;
    unsigned long romSize = 0;
    const uint8_t *rom;
    unsigned frames = argc > 2 ? (unsigned)atoi(argv[2]) : 600;
    int only = argc > 3 ? atoi(argv[3]) : -1;
    int level, donor, failed = 0, played = 0, stopped = 0;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!f) {
        printf("skipped: no ROM (swaps ROM)\n");
        return 0;
    }
    fclose(f);
    engine_default_options(&opt);
    engine = engine_create(argv[1], &opt, err, sizeof err);
    if (!engine) {
        printf("FAIL the engine didn't start: %s\n", err);
        return 1;
    }
    boot = malloc(engine_state_size(engine));
    if (!boot)
        return 1;
    engine_save(engine, boot);
    rom = engine_rom(engine, &romSize);
    /* With a fifth word, one pair in this process, without a child: for a debugger. */
    if (argc > 4)
        return run(engine, boot, rom, romSize, only, atoi(argv[4]), frames);
    for (level = 0; level < LEVELS; level++) {
        if (only >= 0 && level != only)
            continue;
        for (donor = 0; donor < LEVELS; donor++) {
            int status = 0;
            pid_t pid;
            if (donor == level)
                continue;
            fflush(stdout);
            pid = fork();
            if (pid == 0) {
                alarm(SECONDS);
                _exit(run(engine, boot, rom, romSize, level, donor, frames));
            }
            if (pid < 0 || waitpid(pid, &status, 0) < 0) {
                printf("FAIL level %d with level %d's file: no child process\n", level, donor);
                failed++;
                continue;
            }
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                printf("ok          level %d with level %d's file\n", level, donor);
                played++;
            } else if (WIFEXITED(status) && WEXITSTATUS(status) == 2) {
                printf("unplayable  level %d with level %d's file\n", level, donor);
                stopped++;
            } else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
                printf("FAIL hang   level %d with level %d's file\n", level, donor);
                failed++;
            } else {
                printf("FAIL level %d with level %d's file: %s %d\n", level, donor,
                       WIFSIGNALED(status) ? "signal" : "exit", WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
                failed++;
            }
        }
    }
    printf("%s %d pairs played, %d stopped as unplayable, %d failed\n", failed ? "FAIL" : "ok  ", played, stopped,
           failed);
    free(boot);
    engine_free(engine);
    return failed != 0;
}
