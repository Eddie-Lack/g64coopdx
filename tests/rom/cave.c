/* The cave's crystal scene for each player, and players who come into a running level (weemitt's g64players.c and
 * DoCaveStuff in game.c), online, with the ROM. Each case builds a level from the state right after power-on:
 *   A. A player starts the first hub level alone and walks away from the start; a second player comes in later. They
 *      start at the level's start, not beside the first player, and can move.
 *   B. The cave starts with no crystal due; player 0 walks around. Progress made elsewhere then raises the hub's
 *      target (a session event, G64ApplyProgress), and player 1 comes in. Player 1's crystal flies to the fountain.
 *      Player 0 has no scene, and walks exactly as in a run where player 1 never comes.
 *   C. Two players start in the cave with one crystal due. Both fly; one crystal goes in; both can move afterwards.
 *   D. Two players start in the cave with two crystals due. Both go in, each ball to its own pedestal.
 *   E. One player starts in the cave with a crystal due and puts it in. Player 1 comes in with none due (holding the
 *      ball the cave gave at its start). The target then rises: player 1 brought no crystal and has no scene. Player 2
 *      comes in with it due and puts it in.
 *   F. Two players start in the cave as the seventh crystal goes in: the ending holds both.
 *   G. The single-player game (offline, g64Online 0): one player puts the crystal in. The hub level becomes 1 and
 *      the ball goes to y -5000.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: cave ROM. Without a ROM file it prints that it was skipped and exits with 0. */
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
/* The game's GameInfo; caveStage is at 0x55 (weemitt/include/game.h). */
extern unsigned char gameInfo[];
extern unsigned short g64Buttons[16];
extern signed char g64StickX[16], g64StickY[16];
void G64StartPlayers(unsigned present);
void G64SetPresent(unsigned present);
void G64SetStartRecord(int world, const G64Save *s);
void G64ReadSave(G64Save *s);
void G64ApplyProgress(const G64Save *s);
int G64PlayerPosition(int p, float *x, float *y, float *z);
int G64BallPosition(int p, float *x, float *y, float *z);
int G64PlatformPosition(int tag, float *x, float *y, float *z);
void port_game_start_level(int level);
unsigned int port_hash_ram(void);

enum { HUB1 = 0, CAVE = 8 };

static Engine *engine;
static void *boot;
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* A level from the power-on state, with these players and this hub progress. */
static void start(int level, unsigned players, int hubLevel, int hubTarget) {
    G64Save s;
    engine_load(engine, boot);
    g64Online = 1;
    G64StartPlayers(players);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    s.hubLevel = (unsigned char)hubLevel;
    s.hubTarget = (unsigned char)hubTarget;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
}

/* One frame. walk: a bit per player who walks in circles; the others stand still. A presses close the tips until
   frame tips. */
static unsigned tips = 300;
static void frame(unsigned f, unsigned present, unsigned walk) {
    static const signed char dirs[4][2] = {{0, 60}, {60, 0}, {0, -60}, {-60, 0}};
    EngineInput in;
    int p;
    G64SetPresent(present);
    for (p = 0; p < 16; p++) {
        int w = (walk >> p) & 1;
        g64Buttons[p] = f < tips && f % 30 == 0 ? 0x8000 : 0;
        g64StickX[p] = w ? dirs[(f / 25 + (unsigned)p) % 4][0] : 0;
        g64StickY[p] = w ? dirs[(f / 25 + (unsigned)p) % 4][1] : 0;
    }
    memset(&in, 0, sizeof in);
    in.buttons = g64Buttons[0];
    in.x = g64StickX[0];
    in.y = g64StickY[0];
    engine_step(engine, &in);
}

static float dist(const float *a, const float *b) {
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return sqrtf(x * x + y * y + z * z);
}

static void where(int p, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64PlayerPosition(p, &at[0], &at[1], &at[2]);
}

static void ball_at(int p, float *at) {
    at[0] = at[1] = at[2] = 0;
    G64BallPosition(p, &at[0], &at[1], &at[2]);
}

static G64Save save(void) {
    G64Save s;
    G64ReadSave(&s);
    return s;
}

static void case_a(void) {
    float first[3], mine[3], other[3], later[3];
    char what[200];
    unsigned f;
    tips = 600;
    start(HUB1, 0x1, 0, 0);
    for (f = 0; f < 800; f++) {
        frame(f, 0x1, f >= 600 ? 0x1 : 0);
        if (f == 0)
            where(0, first);
    }
    frame(f++, 0x3, 0x1);
    where(1, mine);
    where(0, other);
    snprintf(what, sizeof what, "A: the player who comes later starts %.1f across from the level's start and %.1f "
             "from the other player", sqrtf((mine[0] - first[0]) * (mine[0] - first[0]) +
                                            (mine[2] - first[2]) * (mine[2] - first[2])),
             dist(mine, other));
    /* The player who comes later starts a glove's width (30) from the start spot when the floor is there, or at it. */
    {
        float across = sqrtf((mine[0] - first[0]) * (mine[0] - first[0]) + (mine[2] - first[2]) * (mine[2] - first[2]));
        check(G64PlayerPosition(1, &later[0], &later[1], &later[2]) && (across < 1.0f || fabsf(across - 30.0f) < 1.0f) &&
                  dist(mine, other) > 100.0f,
              what);
    }
    for (; f < 900; f++)
        frame(f, 0x3, 0x3);
    where(1, later);
    snprintf(what, sizeof what, "A: and walks %.1f in 100 frames", dist(mine, later));
    check(dist(mine, later) > 20.0f, what);
    printf("A: hash %08x\n", port_hash_ram());
    tips = 300;
}

/* Case B's run, with player 1 coming in at frame 220 or never. Returns whether player 0 had a scene; path gets player
   0's position every frame from 220 on. */
static unsigned run_b(int join, float (*path)[3]) {
    unsigned f, stage0 = 0;
    G64Save s;
    start(CAVE, 0x1, 0, 0);
    for (f = 0; f < 200; f++)
        frame(f, 0x1, 0x1);
    /* Progress from another instance: a world done, the target raised. */
    memset(&s, 0, sizeof s);
    s.levels = 3;
    s.hubTarget = 1;
    G64ApplyProgress(&s);
    for (; f < 220; f++)
        frame(f, 0x1, 0x1);
    for (; f < 600; f++) {
        frame(f, join ? 0x3 : 0x1, 0x1);
        where(0, path[f - 220]);
        /* The scene's stage of the player in the globals, player 0. */
        stage0 |= gameInfo[0x55];
    }
    return stage0;
}

static void case_b(void) {
    static float alone[380][3], together[380][3];
    float ball[3];
    unsigned stage0, f, differ = 0;
    char what[200];
    run_b(0, alone);
    stage0 = run_b(1, together);
    ball_at(1, ball);
    snprintf(what, sizeof what, "B: the player who came with a crystal due put it in: hub level %u, target %u, their "
             "ball at y %.0f", save().hubLevel, save().hubTarget, ball[1]);
    check(save().hubLevel == 1 && ball[1] == -5000.0f, what);
    for (f = 0; f < 380; f++)
        differ += dist(alone[f], together[f]) != 0.0f;
    snprintf(what, sizeof what, "B: the player who was there first had %s scene, and walked as alone in all but %u of "
             "380 frames", stage0 ? "a" : "no", differ);
    check(stage0 == 0 && differ == 0, what);
    printf("B: hash %08x\n", port_hash_ram());
}

/* The pedestal (platform tag 1 .. 7) that a ball came closest to across, and how close. */
static void closest_pedestal(const float *ball, int *tag, float *best) {
    int k;
    for (k = 1; k <= 7; k++) {
        float x, y, z, d;
        if (!G64PlatformPosition(k, &x, &y, &z))
            continue;
        d = sqrtf((ball[0] - x) * (ball[0] - x) + (ball[2] - z) * (ball[2] - z));
        if (d < *best) {
            *best = d;
            *tag = k;
        }
    }
}

static void case_cd(char name, int target) {
    float a[3], b[3], a2[3], b2[3], ba[3], bb[3], best[2] = {1e9f, 1e9f};
    int tag[2] = {0, 0}, p;
    unsigned f;
    char what[200];
    start(CAVE, 0x3, 0, target);
    for (f = 0; f < 400; f++) {
        frame(f, 0x3, 0);
        /* Where each ball goes while it's still above the floor (a ball put in moves to y -5000). */
        for (p = 0; p < 2; p++) {
            float at[3];
            ball_at(p, at);
            if (at[1] > -1000.0f && f > 60)
                closest_pedestal(at, &tag[p], &best[p]);
        }
    }
    ball_at(0, ba);
    ball_at(1, bb);
    snprintf(what, sizeof what, "%c: %d crystal%s due, two players: hub level %u; both balls gone (y %.0f, %.0f)", name,
             target, target > 1 ? "s" : "", save().hubLevel, ba[1], bb[1]);
    check(save().hubLevel == target && ba[1] == -5000.0f && bb[1] == -5000.0f, what);
    snprintf(what, sizeof what, "%c: the balls reach pedestals %d and %d (%.1f and %.1f away)", name, tag[0], tag[1],
             best[0], best[1]);
    check(best[0] < 5.0f && best[1] < 5.0f && tag[0] >= 1 && tag[1] >= 1 && tag[0] <= target && tag[1] <= target &&
              (target == 1 || tag[0] != tag[1]),
          what);
    where(0, a);
    where(1, b);
    for (; f < 500; f++)
        frame(f, 0x3, 0x3);
    where(0, a2);
    where(1, b2);
    snprintf(what, sizeof what, "%c: afterwards both walk: %.1f and %.1f in 100 frames", name, dist(a, a2), dist(b, b2));
    check(dist(a, a2) > 20.0f && dist(b, b2) > 20.0f, what);
    printf("%c: hash %08x\n", name, port_hash_ram());
}

static void case_e(void) {
    float ball[3];
    unsigned f;
    char what[200];
    G64Save s;
    int levelBefore;
    start(CAVE, 0x1, 0, 1);
    for (f = 0; f < 300; f++)
        frame(f, 0x1, 0);
    for (; f < 350; f++)
        frame(f, 0x3, 0);
    memset(&s, 0, sizeof s);
    s.levels = 7;
    s.hubTarget = 2;
    G64ApplyProgress(&s);
    for (; f < 600; f++)
        frame(f, 0x3, 0);
    levelBefore = save().hubLevel;
    ball_at(1, ball);
    snprintf(what, sizeof what, "E: the player who came with no crystal due has no scene when the target rises: hub "
             "level %d, their ball at y %.0f", levelBefore, ball[1]);
    check(levelBefore == 1 && ball[1] != -5000.0f, what);
    for (; f < 900; f++)
        frame(f, 0x7, 0);
    ball_at(2, ball);
    snprintf(what, sizeof what, "E: the player who came with it due puts it in: hub level %u, their ball at y %.0f",
             save().hubLevel, ball[1]);
    check(save().hubLevel == 2 && ball[1] == -5000.0f, what);
    printf("E: hash %08x\n", port_hash_ram());
}

static void case_f(void) {
    float a[3], b[3], a2[3], b2[3];
    unsigned f;
    char what[200];
    start(CAVE, 0x3, 6, 7);
    for (f = 0; f < 200; f++)
        frame(f, 0x3, 0);
    where(0, a);
    where(1, b);
    for (; f < 260; f++)
        frame(f, 0x3, 0x3);
    where(0, a2);
    where(1, b2);
    snprintf(what, sizeof what, "F: the ending (hub level %u) holds both players: they walk %.1f and %.1f", save().hubLevel,
             dist(a, a2), dist(b, b2));
    check(save().hubLevel == 7 && dist(a, a2) < 1.0f && dist(b, b2) < 1.0f, what);
}

static void case_g(void) {
    float ball[3];
    unsigned f, h = 2166136261u;
    char what[200];
    G64Save s;
    engine_load(engine, boot);
    g64Online = 0;
    G64StartPlayers(0x1);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    s.hubTarget = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(CAVE);
    for (f = 0; f < 300; f++) {
        frame(f, 0x1, 0);
        if ((f + 1) % 50 == 0)
            h = (h ^ port_hash_ram()) * 16777619u;
    }
    ball_at(0, ball);
    snprintf(what, sizeof what, "G: single-player: the crystal goes in: hub level %u, the ball at y %.0f", save().hubLevel,
             ball[1]);
    check(save().hubLevel == 1 && ball[1] == -5000.0f, what);
    printf("G: hash %08x\n", h);
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    if (!rom) {
        printf("skipped: no ROM (cave ROM)\n");
        return 0;
    }
    fclose(rom);
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
    case_a();
    case_b();
    case_cd('C', 1);
    case_cd('D', 2);
    case_e();
    case_f();
    case_g();
    printf("%s each player has their own crystal scene\n", failed ? "FAIL" : "ok  ");
    free(boot);
    engine_free(engine);
    return failed;
}
