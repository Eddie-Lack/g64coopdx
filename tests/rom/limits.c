/* The frame's limits with 16 players (weemitt's object.c, jalloc.c and main.c), online, with the ROM.
 * The N64 game has room for 256 object matrices a frame, 1280 heap blocks and 20000 bytes in a display list task.
 * One player stays inside them. Sixteen don't: a glove takes up to 14 matrices, and each player's glove, ball,
 * shadows and footprints take heap blocks. g64coopdx's copy of the game has more room for each.
 * Three levels are built from the state right after power-on with 16 players and run for 800 frames, with every
 * player walking their own way from frame 500: the first hub (1), the Atlantis hub (12, the most heap blocks) and
 * the Prehistoric boss (28, the longest display list). For each level the test checks that
 *   - all 16 players exist at the end,
 *   - the frame used more than 256 matrices and no more than the room for them,
 *   - the heap's blocks stayed inside their room,
 *   - a frame's display list stayed inside its buffer.
 * Over the three levels it checks that the N64 game's limits were passed: more than 1280 blocks, and a task of more
 * than 20000 bytes. It prints the peaks and a memory hash per level: make twoplayers runs it on Linux and under Wine
 * and compares the outputs.
 *
 * Usage: limits ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

/* The game's own counters. glistp and dynamicp are pointers stored as 32-bit words: only their difference is used.
   The display list starts 0x4100 bytes into Dynamic (weemitt/include/display.h). */
extern int objectMatrix;
extern unsigned glistp, dynamicp;
extern int actualGFXListLength[];
extern unsigned char jallocControl[];
int JallocFreeBlocks(void);

enum {
    PLAYERS = 16,
    FRAMES = 800,
    MATRICES_N64 = 256,
    MATRICES = 256 + 768,
    BLOCKS_N64 = 0x500,
    TASK_N64 = 20000,
    LIST = 20000
};

static Engine *engine;
static void *boot;
static int failed;
static int mostBlocks, largestTask;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

static void level(int num) {
    G64Save s;
    unsigned f;
    int p, t, players = 0;
    int matrices = 0, blocks = 0, list = 0, task = 0, leastFree = 1 << 30;
    float x, y, z;

    engine_load(engine, boot);
    g64Online = 1;
    G64StartPlayers(0xFFFF);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(num);
    for (f = 0; f < FRAMES; f++) {
        EngineInput in;
        const int *jc = (const int *)(jallocControl + BLOCKS_N64 * 20); /* heapStart, heapSize, lastStatic, firstDynamic */
        int used, commands;
        G64SetPresent(0xFFFF);
        for (p = 0; p < PLAYERS; p++) {
            g64Buttons[p] = f % 30 == 0 ? 0x8000 : 0;
            g64StickX[p] = f < 500 ? 0 : (signed char)(p * 37 % 120 - 60);
            g64StickY[p] = f < 500 ? 0 : (signed char)(p * 53 % 120 - 60);
        }
        memset(&in, 0, sizeof in);
        in.buttons = g64Buttons[0];
        in.x = g64StickX[0];
        in.y = g64StickY[0];
        engine_step(engine, &in);
        used = jc[2] + 1 + BLOCKS_N64 - jc[3];
        commands = (int)((glistp - dynamicp - 0x4100) / 8);
        if (objectMatrix > matrices)
            matrices = objectMatrix;
        if (used > blocks)
            blocks = used;
        if (JallocFreeBlocks() < leastFree)
            leastFree = JallocFreeBlocks();
        if (commands > list)
            list = commands;
        for (t = 0; t < 6; t++)
            if (actualGFXListLength[t] > task)
                task = actualGFXListLength[t];
    }
    for (p = 0; p < PLAYERS; p++)
        players += G64PlayerPosition(p, &x, &y, &z);
    printf("level %d: %d players, %d matrices, %d blocks, %d list commands, largest task %d bytes, hash %08x\n", num,
           players, matrices, blocks, list, task, port_hash_ram());
    check(players == PLAYERS, "all 16 players exist");
    check(matrices > MATRICES_N64 && matrices <= MATRICES, "more than 256 matrices, inside the room for them");
    check(leastFree > 0, "the heap's blocks stayed inside their room");
    check(list < LIST, "the display list stayed inside its buffer");
    if (blocks > mostBlocks)
        mostBlocks = blocks;
    if (task > largestTask)
        largestTask = task;
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (limits ROM)\n");
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
    level(1);
    level(12);
    level(28);
    check(mostBlocks > BLOCKS_N64, "a level used more than 1280 heap blocks");
    check(largestTask > TASK_N64, "a display list task was larger than 20000 bytes");
    printf("%s 16 players fit in a frame\n", failed ? "FAIL" : "ok  ");
    free(boot);
    engine_free(engine);
    return failed;
}
