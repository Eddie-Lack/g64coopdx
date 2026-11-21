/* Puzzles with more than one player (weemitt's puzzle.c, PuzzleEvaluateCondition, and g64players.c), online, with
 * the ROM. Each case builds a level from the state right after power-on, with two players, runs it to frame 400, and
 * then puts a glove or a ball on a puzzle's platform for 40 frames. A puzzle with one condition, a glove or a ball that
 * just came onto a tagged platform, fires:
 *   - when player 0 alone is put there;
 *   - when player 1 is put there and player 0 stays at the start;
 *   - not when nobody is put there.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: puzzles ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
void G64PlacePlayer(int p, float x, float y, float z);
void G64PlaceBall(int p, float x, float y, float z);
int G64PlatformPosition(int tag, float *x, float *y, float *z);
int G64PuzzleInfo(int k, int *timesLeft, int *active, int *count);
int G64PuzzleCondition(int k, int j, int *type, int *tag, int *value);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { READY = 400, RUN = 60, PLACE = 40 };
/* weemitt's enum ConditionType. */
enum { HAND_JUST_ON = 7, BALL_JUST_ON = 9 };

static Engine *engine;
static void *boot, *ready;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

static void frame(unsigned f, unsigned present) {
    EngineInput in;
    int p;
    G64SetPresent(present);
    for (p = 0; p < 16; p++) {
        g64Buttons[p] = f < 300 && f % 30 == 0 ? 0x8000 : 0;
        g64StickX[p] = g64StickY[p] = 0;
    }
    memset(&in, 0, sizeof in);
    in.buttons = g64Buttons[0];
    engine_step(engine, &in);
}

/* RUN frames from the READY state; who (-1: nobody) has their glove or ball put on the platform with this tag for
   the first PLACE frames. Returns the frame the puzzle fired at (its count went down or its actions started), or -1. */
static int run(int k, int tag, int who, int ball, unsigned present) {
    int t0, a0, c, t1, a1, g;
    float x, y, z;
    engine_load(engine, ready);
    G64PuzzleInfo(k, &t0, &a0, &c);
    G64PlatformPosition(tag, &x, &y, &z);
    for (g = 0; g < RUN; g++) {
        frame(READY + (unsigned)g, present);
        if (who >= 0 && g < PLACE) {
            if (ball)
                G64PlaceBall(who, x, y + 12.0f, z);
            else
                G64PlacePlayer(who, x, y + 12.0f, z);
        }
        G64PuzzleInfo(k, &t1, &a1, &c);
        if (t1 != t0 || (a1 && !a0))
            return g;
    }
    return -1;
}

static void puzzle_case(int level, int k, int wantType) {
    G64Save s;
    unsigned f;
    int tl, active, count, type, tag, value, alone, nobody, second;
    float x, y, z;
    char what[240];
    engine_load(engine, boot);
    g64Online = 1;
    G64StartPlayers(0x3);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
    for (f = 0; f < READY; f++)
        frame(f, 0x3);
    engine_save(engine, ready);
    if (!G64PuzzleInfo(k, &tl, &active, &count) || !G64PuzzleCondition(k, 0, &type, &tag, &value) ||
        !G64PlatformPosition(tag, &x, &y, &z)) {
        snprintf(what, sizeof what, "level %d has no puzzle %d with a platform", level, k);
        check(0, what);
        return;
    }
    alone = run(k, tag, 0, type == BALL_JUST_ON, 0x1);
    nobody = run(k, tag, -1, type == BALL_JUST_ON, 0x3);
    second = run(k, tag, 1, type == BALL_JUST_ON, 0x3);
    snprintf(what, sizeof what, "level %d, puzzle %d (a %s just on platform %d): fires at frame %d for player 0 alone, "
             "%d for player 1, %d for nobody (-1: never)", level, k, type == BALL_JUST_ON ? "ball" : "glove", tag,
             alone, second, nobody);
    check(type == wantType && count == 1 && value == 1 && alone >= 0 && second >= 0 && nobody < 0, what);
    printf("hash %08x\n", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (puzzles ROM)\n");
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
    puzzle_case(15, 51, BALL_JUST_ON);
    puzzle_case(16, 16, HAND_JUST_ON);
    puzzle_case(27, 12, BALL_JUST_ON);
    puzzle_case(36, 5, HAND_JUST_ON);
    printf("%s any player's glove or ball works a puzzle\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
