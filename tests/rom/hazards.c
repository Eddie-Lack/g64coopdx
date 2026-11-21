/* World hazards with more than one player (weemitt's wind.c, water.c, spike.c and buzzer.c, and g64players.c,
 * "World hazards"), online, with the ROM. Each case builds a level from the state right after power-on, with two
 * players, runs it to frame 400, and then puts a glove at a hazard.
 *   - A wind (level 20) carries player 1's glove as far as player 0's alone.
 *   - Water (levels 9 and 12) takes player 1's glove: its water state is set, and a current carries it as it
 *     carries player 0's alone.
 *   - A spike (levels 12 and 20) hurts player 1 as it hurts player 0 alone: one health less, and the same knock back.
 * In every case player 0, who stays at the start, keeps their health.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: hazards ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
int G64HazardPlace(int kind, int k, float *x, float *y, float *z);
int G64PlayerHealth(int p);
int G64PlayerInWater(int p);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { READY = 400, RUN = 40 };
enum { WIND, WATER, SPIKE };

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

/* What RUN frames from the READY state did to who, put at hazard k of a kind: where the glove ended, from the
   hazard's place; their health; their glove's highest water state; and player 0's health. */
typedef struct {
    float end[3];
    int health, water, health0;
} Result;

static void run(int kind, int k, int who, unsigned present, Result *r) {
    float x = 0, y = 0, z = 0;
    int g;
    engine_load(engine, ready);
    G64HazardPlace(kind, k, &x, &y, &z);
    r->water = 0;
    for (g = 0; g < RUN; g++) {
        /* Water gets three frames at its surface; a spike is fallen onto. */
        if (g < (kind == WATER ? 3 : 1))
            G64PlacePlayer(who, x, y + (kind == SPIKE ? 12.0f : 0.0f), z);
        frame(READY + (unsigned)g, present);
        if (G64PlayerInWater(who) > r->water)
            r->water = G64PlayerInWater(who);
    }
    G64PlayerPosition(who, &r->end[0], &r->end[1], &r->end[2]);
    r->end[0] -= x;
    r->end[1] -= y;
    r->end[2] -= z;
    r->health = G64PlayerHealth(who);
    r->health0 = G64PlayerHealth(0);
}

static void hazard_case(int level, int kind, int k) {
    static const char *names[3] = {"wind", "water", "spike"};
    G64Save s;
    unsigned f;
    Result alone, second;
    float across, apart;
    int before, effect;
    char what[300];
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
    before = G64PlayerHealth(1);
    run(kind, k, 0, 0x1, &alone);
    run(kind, k, 1, 0x3, &second);
    across = sqrtf(second.end[0] * second.end[0] + second.end[2] * second.end[2]);
    apart = sqrtf((second.end[0] - alone.end[0]) * (second.end[0] - alone.end[0]) +
                  (second.end[1] - alone.end[1]) * (second.end[1] - alone.end[1]) +
                  (second.end[2] - alone.end[2]) * (second.end[2] - alone.end[2]));
    /* The hazard's own mark: a wind or a current carries the glove across; water sets the water state; a spike
       takes one health. */
    effect = kind == WIND ? across > 50.0f : kind == WATER ? second.water > 0 : second.health == before - 1;
    snprintf(what, sizeof what, "level %d, %s %d: player 1's glove ends %.0f across and %.1f from where player 0's "
             "ends alone; health %d to %d (alone %d), water state %d (alone %d); player 0 keeps %d", level,
             names[kind], k, across, apart, before, second.health, alone.health, second.water, alone.water,
             second.health0);
    check(effect && apart < 1.5f && second.health == alone.health && second.water == alone.water &&
              second.health0 == before, what);
    printf("hash %08x\n", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (hazards ROM)\n");
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
    hazard_case(20, WIND, 0);
    hazard_case(9, WATER, 0);
    hazard_case(12, WATER, 2);
    hazard_case(12, SPIKE, 1);
    hazard_case(20, SPIKE, 0);
    printf("%s winds, water and spikes act on every player\n", failed ? "FAIL" : "ok  ");
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
