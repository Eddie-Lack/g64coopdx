/* The mouse in the online game (weemitt's src/port/mouselook.c), with the ROM. Made-up mouse motion and clicks go
 * through the same mapping as the window's, and the buttons it gives are each frame's input, as in the online
 * program. A level is built from the state right after power-on, with two players, and run until the second
 * player's slot exists (frame 400).
 *   A. Mouse motion to the left turns the player's camera to the left, and the same motion to the right turns it
 *      back by about as much. Player 0 and player 1 turn alike.
 *   B. Turning the view right, the camera meets a wall: it touches the wall's face and never passes to its other
 *      side, it stays above the glove, and the turn stops.
 *   C. The clicks act as the game's own buttons: without the ball, the left click jumps and the right click points
 *      (or snatches the ball when it's near). With the ball, the left click bounces it, a quick right click lobs it
 *      and a held right click winds up and slaps it. Both ball controls (easy and normal) give the same.
 * It prints what it checks and a memory hash per case: make twoplayers runs it on Linux and under Wine and compares
 * the outputs.
 *
 * Usage: mouselook ROM. Without a ROM file it prints that it was skipped and exits with 0. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../weemitt/bruteforce/engine/engine.h"
#include "../../weemitt/src/port/mouselook.h"

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
int G64BallPosition(int p, float *x, float *y, float *z);
int G64SoundListener(int p, float *pos, float *yaw, int *maxDist);
int G64PlayerHoldsBall(int p);
int G64PlayerHandMode(int p);
int G64CameraContact(int p, float *plane, float *pos);
void port_game_start_level(int level);
void port_set_easy_ball(int on);
unsigned int port_hash_ram(void);

/* The glove's modes (weemitt's include/enums.h). */
enum { JUMP = 2, PREWHACK = 5, WHACK = 6, BOUNCE = 14, LOB = 30, SNATCH = 34, LOCATE = 42 };
enum { READY = 400, PLAYERS = 2 };

static Engine *engine;
static void *boot, *ready, *holding[PLAYERS];
static MouseLook mouse[PLAYERS];
static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failed |= !ok;
}

/* One frame. Each player's mouse steps once and gives buttons, which go in with the player's stick. */
static void frame(const signed char *sx, const signed char *sy) {
    EngineInput in;
    int p;
    unsigned short b[PLAYERS];
    for (p = 0; p < PLAYERS; p++)
        b[p] = ml_step(&mouse[p], G64PlayerHoldsBall(p), 0);
    G64SetPresent((1u << PLAYERS) - 1);
    for (p = 0; p < 16; p++) {
        g64Buttons[p] = p < PLAYERS ? b[p] : 0;
        g64StickX[p] = p < PLAYERS && sx ? sx[p] : 0;
        g64StickY[p] = p < PLAYERS && sy ? sy[p] : 0;
    }
    memset(&in, 0, sizeof in);
    in.buttons = g64Buttons[0];
    in.x = g64StickX[0];
    in.y = g64StickY[0];
    engine_step(engine, &in);
}

static void reset_mice(void) {
    int p;
    for (p = 0; p < PLAYERS; p++)
        ml_reset(&mouse[p]);
}

static float yaw_of(int p) {
    float pos[3], yaw = 0;
    int max;
    G64SoundListener(p, pos, &yaw, &max);
    return yaw;
}

/* The signed turn from a to b, the short way round. */
static float turn(float a, float b) {
    float d = b - a;
    while (d > 3.1415927f)
        d -= 6.2831853f;
    while (d < -3.1415927f)
        d += 6.2831853f;
    return d;
}

static void start(int level) {
    G64Save s;
    unsigned f;
    engine_load(engine, boot);
    g64Online = 1;
    G64StartPlayers((1u << PLAYERS) - 1);
    memset(&s, 0, sizeof s);
    s.levels = 1;
    G64SetStartRecord(0, &s);
    port_game_start_level(level);
    reset_mice();
    for (f = 0; f < READY; f++) {
        /* A for the level's opening text, as the other tests press it. */
        if (f < 300 && f % 30 == 0) {
            ml_button(&mouse[0], ML_LEFT, 1);
            ml_button(&mouse[0], ML_LEFT, 0);
            ml_button(&mouse[1], ML_LEFT, 1);
            ml_button(&mouse[1], ML_LEFT, 0);
        }
        frame(NULL, NULL);
    }
    engine_save(engine, ready);
}

/* Turns player who's view by moving the mouse dx counts a frame for n frames; returns the camera's turn. Positive is
   to the left (the camera's angle grows). */
static float mouse_turn(int who, int dx, int n) {
    float y0 = yaw_of(who), prev = y0, total = 0;
    int f;
    for (f = 0; f < n; f++) {
        float y;
        ml_motion(&mouse[who], dx, 0, ML_SENS_DEFAULT);
        frame(NULL, NULL);
        y = yaw_of(who);
        total += turn(prev, y);
        prev = y;
    }
    return total;
}

static void case_a(void) {
    float left[PLAYERS], back[PLAYERS], slow;
    char what[240];
    int p;
    for (p = 0; p < PLAYERS; p++) {
        engine_load(engine, ready);
        reset_mice();
        left[p] = mouse_turn(p, -84, 10);
        back[p] = mouse_turn(p, 84, 10);
    }
    engine_load(engine, ready);
    reset_mice();
    slow = mouse_turn(0, -42, 10);
    snprintf(what, sizeof what, "A: mouse motion to the left turns player 0's camera by %.2f and player 1's by %.2f; "
             "back to the right by %.2f and %.2f; half the speed turns %.2f",
             left[0], left[1], back[0], back[1], slow);
    check(left[0] > 0.8f && fabsf(left[0] - left[1]) < 0.2f && back[0] < -0.5f && back[1] < -0.5f
              && fabsf(left[0] + back[0]) < 0.6f && slow > 0.25f * left[0] && slow < 0.75f * left[0],
          what);
    printf("A: hash %08x\n", port_hash_ram());
}

/* B: the view turns right into a wall for three seconds. The face the camera touches first is the wall: the camera
   stays on the side of it where it was, and the turn stops. */
static void case_b(void) {
    float plane[4], first[4] = {0, 0, 0, 0}, pos[3], glove[3], side0 = 0, worst = 1e9f, total = 0, late = 0;
    float prev, lowest = 1e9f;
    char what[300];
    int f, touched = -1, crossed = 0;
    engine_load(engine, ready);
    reset_mice();
    prev = yaw_of(0);
    for (f = 0; f < 90; f++) {
        float y, side;
        ml_motion(&mouse[0], 200, 0, ML_SENS_DEFAULT);
        frame(NULL, NULL);
        y = yaw_of(0);
        total += turn(prev, y);
        if (f >= 60)
            late += turn(prev, y);
        prev = y;
        if (G64CameraContact(0, plane, pos) && touched < 0) {
            touched = f;
            memcpy(first, plane, sizeof first);
            side0 = first[0] * pos[0] + first[1] * pos[1] + first[2] * pos[2] + first[3];
        }
        G64CameraContact(0, plane, pos);
        G64PlayerPosition(0, &glove[0], &glove[1], &glove[2]);
        if (pos[1] - glove[1] < lowest)
            lowest = pos[1] - glove[1];
        if (touched >= 0) {
            side = first[0] * pos[0] + first[1] * pos[1] + first[2] * pos[2] + first[3];
            if ((side0 >= 0) != (side >= 0))
                crossed++;
            if (fabsf(side) < worst)
                worst = fabsf(side);
        }
    }
    snprintf(what, sizeof what, "B: turning right, the camera touches a face at frame %d (normal %.2f %.2f %.2f); it "
             "stays %.1f or more on its own side of it and never crosses (%d frames on the other side); it turns "
             "%.2f in all and %.3f in the last second; it stays %.0f or more above the glove",
             touched, first[0], first[1], first[2], worst, crossed, total, late, lowest);
    check(touched >= 0 && touched < 60 && fabsf(first[1]) < 0.7f && crossed == 0 && total > -3.0f
              && fabsf(late) < 0.02f && lowest > 0.0f,
          what);
    printf("B: hash %08x\n", port_hash_ram());
}

/* Steers player who's glove to their ball until it holds it. Returns 1 when it does. */
static int take_ball(int who) {
    int f;
    reset_mice();
    for (f = 0; f < 600; f++) {
        float b[3], g[3], c[3], yaw, wx, wz, len;
        int max;
        signed char sx[PLAYERS] = {0, 0}, sy[PLAYERS] = {0, 0};
        G64BallPosition(who, &b[0], &b[1], &b[2]);
        G64PlayerPosition(who, &g[0], &g[1], &g[2]);
        G64SoundListener(who, c, &yaw, &max);
        wx = b[0] - g[0];
        wz = b[2] - g[2];
        len = sqrtf(wx * wx + wz * wz);
        if (len < 1.0f)
            len = 1.0f;
        wx /= len;
        wz /= len;
        /* The camera looks along (sin yaw, cos yaw); the stick's right is (-cos yaw, sin yaw). */
        sx[who] = (signed char)lroundf((-wx * cosf(yaw) + wz * sinf(yaw)) * 60.0f);
        sy[who] = (signed char)lroundf((wx * sinf(yaw) + wz * cosf(yaw)) * 60.0f);
        frame(sx, sy);
        if (G64PlayerHoldsBall(who)) {
            for (f = 0; f < 40; f++)
                frame(NULL, NULL);
            return G64PlayerHoldsBall(who);
        }
    }
    return 0;
}

/* From state st: a click of the button that lasts frames (0: shorter than one frame), then 40 frames. Returns
   the modes the glove went through, as bits. */
static unsigned long long click(void *st, int who, int button, int frames) {
    unsigned long long modes = 0;
    int f;
    engine_load(engine, st);
    reset_mice();
    ml_button(&mouse[who], button, 1);
    if (frames == 0)
        ml_button(&mouse[who], button, 0);
    for (f = 0; f < 40 + frames; f++) {
        if (frames > 0 && f == frames)
            ml_button(&mouse[who], button, 0);
        frame(NULL, NULL);
        if (G64PlayerHandMode(who) >= 0 && G64PlayerHandMode(who) < 64)
            modes |= 1ull << G64PlayerHandMode(who);
    }
    return modes;
}

#define HAS(m, x) (((m) >> (x)) & 1)

static void case_c(int easy) {
    unsigned long long jump, point, bounce, lob, slap;
    char what[300];
    int p, held = 1;
    port_set_easy_ball(easy);
    start(10);
    for (p = 0; p < PLAYERS; p++) {
        engine_load(engine, ready);
        held &= take_ball(p);
        engine_save(engine, holding[p]);
    }
    check(held, "C: each player's glove takes their ball");
    for (p = 0; p < PLAYERS; p++) {
        jump = click(ready, p, ML_LEFT, 2);
        point = click(ready, p, ML_RIGHT, 20);
        bounce = click(holding[p], p, ML_LEFT, 0);
        lob = click(holding[p], p, ML_RIGHT, 3);
        slap = click(holding[p], p, ML_RIGHT, 20);
        snprintf(what, sizeof what, "C: %s ball, player %d: without the ball the left click jumps (%d) and the right "
                 "click points (%d or %d); with it the left click bounces (%d), a quick right click lobs (%d, no slap "
                 "%d) and a held one slaps (%d, %d, no lob %d)",
                 easy ? "easy" : "normal", p, (int)HAS(jump, JUMP), (int)HAS(point, LOCATE), (int)HAS(point, SNATCH),
                 (int)HAS(bounce, BOUNCE), (int)HAS(lob, LOB), (int)HAS(lob, WHACK), (int)HAS(slap, PREWHACK),
                 (int)HAS(slap, WHACK), (int)HAS(slap, LOB));
        check(HAS(jump, JUMP) && (HAS(point, LOCATE) || HAS(point, SNATCH)) && !HAS(point, JUMP)
                  && HAS(bounce, BOUNCE) && HAS(lob, LOB) && !HAS(lob, WHACK) && !HAS(lob, PREWHACK)
                  && HAS(slap, PREWHACK) && HAS(slap, WHACK) && !HAS(slap, LOB),
              what);
    }
    printf("C: hash %08x\n", port_hash_ram());
}

int main(int argc, char **argv) {
    EngineOptions opt;
    char err[256];
    FILE *rom = argc > 1 ? fopen(argv[1], "rb") : NULL;
    int p;
    if (!rom) {
        printf("skipped: no ROM (mouselook ROM)\n");
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
    for (p = 0; p < PLAYERS; p++)
        holding[p] = malloc(engine_state_size(engine));
    if (!boot || !ready || !holding[0] || !holding[1])
        return 1;
    engine_save(engine, boot);
    port_set_easy_ball(1);
    start(10);
    case_a();
    case_b();
    case_c(1);
    case_c(0);
    printf("%s the mouse turns the camera through the game and clicks as the game's buttons\n",
           failed ? "FAIL" : "ok  ");
    for (p = 0; p < PLAYERS; p++)
        free(holding[p]);
    free(ready);
    free(boot);
    engine_free(engine);
    return failed;
}
