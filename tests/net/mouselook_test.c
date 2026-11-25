/* The mouse and the direction keys as a controller (weemitt's src/port/mouselook.h), without the game: made-up mouse
 * motion and clicks go in, and each game frame's buttons and stick are checked. The ROM test (tests/rom/mouselook.c)
 * plays the same mapping in the game.
 *
 * It prints each check and ends with a line of failures: make test-windows compares the Linux and Windows outputs. */
#include <stdio.h>
#include <string.h>

#include "../../weemitt/src/port/mouselook.h"

#define A ML_BTN_A
#define B ML_BTN_B
#define CL ML_BTN_CLEFT
#define CR ML_BTN_CRIGHT

static int failures;
static MouseLook m;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += !ok;
}

/* Runs n frames and returns how many of them hold all of want; *mask gets every button they held. */
static int frames(int n, int holds, int plain, unsigned short want, unsigned short *mask) {
    int f, count = 0;
    unsigned short all = 0;
    for (f = 0; f < n; f++) {
        unsigned short b = ml_step(&m, holds, plain);
        all |= b;
        if ((b & want) == want)
            count++;
    }
    if (mask)
        *mask = all;
    return count;
}

/* The frame numbers in which button want is held, from the frame of the step after start, as a bit mask. */
static unsigned long pattern(int n, int holds, unsigned short want) {
    unsigned long bits = 0;
    int f;
    for (f = 0; f < n; f++)
        if (ml_step(&m, holds, 0) & want)
            bits |= 1ul << f;
    return bits;
}

static void clicks(void) {
    unsigned short all;
    unsigned long p;
    int f;

    /* Without the ball the left click is A (a jump) and the right click is B (a point). */
    ml_reset(&m);
    ml_button(&m, ML_LEFT, 1);
    ml_button(&m, ML_LEFT, 0);
    check(frames(1, 0, 0, A, &all) == 1 && all == A, "a left click shorter than a frame gives one frame of A");
    check(frames(5, 0, 0, 0, &all) == 5 && all == 0, "and nothing after it");
    ml_button(&m, ML_LEFT, 1);
    check(frames(8, 0, 0, A, &all) == 8 && all == A, "a held left click holds A");
    ml_button(&m, ML_LEFT, 0);
    check(frames(3, 0, 0, 0, &all) == 3 && all == 0, "and lets go of it with the click");
    ml_button(&m, ML_RIGHT, 1);
    check(frames(20, 0, 0, B, &all) == 20 && all == B, "a held right click holds B: the glove points");
    ml_button(&m, ML_RIGHT, 0);
    check(frames(1, 0, 0, 0, &all) == 1 && all == 0, "and lets go of it");

    /* With the ball the left click is B: a tap bounces the ball, a hold aims a throw. */
    ml_reset(&m);
    ml_button(&m, ML_LEFT, 1);
    ml_button(&m, ML_LEFT, 0);
    check(frames(1, 1, 0, B, &all) == 1 && all == B, "with the ball, a left click gives one frame of B: a bounce");
    ml_button(&m, ML_LEFT, 1);
    check(frames(10, 1, 0, B, &all) == 10 && all == B, "with the ball, a held left click holds B: a throw's aim");
    ml_button(&m, ML_LEFT, 0);
    check(frames(2, 1, 0, 0, &all) == 2 && all == 0, "and lets go of it: the throw");

    /* With the ball the right click is A. A quick click waits, then gives two frames of A: a lob. */
    ml_reset(&m);
    ml_button(&m, ML_RIGHT, 1);
    p = pattern(3, 1, A);
    ml_button(&m, ML_RIGHT, 0);
    p |= pattern(6, 1, A) << 3;
    check(p == 3ul << 3, "with the ball, a right click of three frames gives two frames of A after it: a lob");
    ml_button(&m, ML_RIGHT, 1);
    ml_button(&m, ML_RIGHT, 0);
    p = pattern(4, 1, A);
    check(p == 3ul, "a right click shorter than a frame is a lob too");

    /* Held past ML_LOB_FRAMES, it holds A until the click ends: a slap. */
    ml_reset(&m);
    ml_button(&m, ML_RIGHT, 1);
    p = pattern(20, 1, A);
    ml_button(&m, ML_RIGHT, 0);
    p |= pattern(4, 1, A) << 20;
    check(p == ((1ul << 20) - 1) - ((1ul << (ML_LOB_FRAMES - 1)) - 1),
          "a held right click holds A from its fourth frame to its end: the wind-up and the slap");
    ml_button(&m, ML_RIGHT, 1);
    p = pattern(ML_LOB_FRAMES, 1, A);
    ml_button(&m, ML_RIGHT, 0);
    p |= pattern(8, 1, A) << ML_LOB_FRAMES;
    for (f = 0; f < 32 && !(p >> f & 1); f++)
        ;
    check((p >> f) == (1ul << ML_SLAP_FRAMES) - 1,
          "a right click let go just after the wait still holds A for four frames: a slap, not a lob");

    /* The button is chosen when the click starts. */
    ml_reset(&m);
    ml_button(&m, ML_LEFT, 1);
    frames(2, 0, 0, 0, NULL);
    check(frames(4, 1, 0, A, &all) == 4 && all == A,
          "a left click that started without the ball stays A when the glove takes the ball");
    ml_button(&m, ML_LEFT, 0);
    frames(1, 1, 0, 0, NULL);
    ml_button(&m, ML_RIGHT, 1);
    frames(2, 1, 0, 0, NULL);
    check(frames(10, 0, 0, A, &all) == 10 - (ML_LOB_FRAMES - 3) && all == A,
          "a right click that started with the ball stays A when the glove lets go of it");
    ml_button(&m, ML_RIGHT, 0);

    /* In a menu or a tip, left is A and right is B, with the ball or without it. */
    ml_reset(&m);
    ml_button(&m, ML_LEFT, 1);
    check(frames(3, 1, 1, A, &all) == 3 && all == A, "in a menu or a tip, the left click is A");
    ml_button(&m, ML_LEFT, 0);
    ml_button(&m, ML_RIGHT, 1);
    frames(1, 1, 1, 0, NULL);
    check(frames(3, 1, 1, B, &all) == 3 && all == B, "and the right click is B, at once");
    ml_button(&m, ML_RIGHT, 0);

    /* Letting go of the mouse ends the clicks. */
    ml_button(&m, ML_LEFT, 1);
    ml_button(&m, ML_RIGHT, 1);
    ml_reset(&m);
    check(frames(4, 0, 0, 0, &all) == 4 && all == 0, "when the window lets go of the mouse, its buttons end");
}

static void camera(void) {
    unsigned short all;
    int f, n;

    ml_reset(&m);
    ml_motion(&m, 84, 0, 10);
    check(frames(1, 0, 0, CL, &all) == 1 && all == CL,
          "84 counts to the right at sensitivity 10 give one frame of C-left: the view turns right");
    check(frames(4, 0, 0, 0, &all) == 4 && all == 0, "and then the camera stops");
    ml_motion(&m, -84, 0, 10);
    check(frames(5, 0, 0, CR, &all) == 1 && all == CR, "84 counts to the left give one frame of C-right");
    ml_motion(&m, 0, 500, 10);
    check(frames(5, 0, 0, 0, &all) == 5 && all == 0, "up and down don't move the camera");
    ml_motion(&m, 84, 0, 20);
    check(frames(5, 0, 0, CL, &all) == 2, "at sensitivity 20 the same motion turns twice as far");
    ml_motion(&m, 84 * 4, 0, 5);
    check(frames(5, 0, 0, CL, &all) == 2, "at sensitivity 5, four times the motion turns as far");

    /* A slow motion presses C in some frames and not others: half the speed, half the frames. */
    ml_reset(&m);
    n = 0;
    for (f = 0; f < 40; f++) {
        ml_motion(&m, 42, 0, 10);
        n += (ml_step(&m, 0, 0) & CL) != 0;
    }
    check(n == 20, "42 counts a frame, half of a C press, press C-left in 20 of 40 frames");

    /* A fast motion is held to the C button's own speed, and what's beyond the cap is dropped. */
    ml_reset(&m);
    ml_motion(&m, 100000, 0, 40);
    check(frames(10, 0, 0, CL, &all) == ML_TURN_CAP && all == CL,
          "a huge motion turns for three frames at most, at the C button's own speed");
    ml_motion(&m, -2147483647, 0, 40);
    check(frames(10, 0, 0, CR, &all) == ML_TURN_CAP, "a motion of the most counts an event can hold is capped too");

    /* Motion and a click at once. */
    ml_reset(&m);
    ml_motion(&m, 84, 0, 10);
    ml_button(&m, ML_LEFT, 1);
    check(ml_step(&m, 0, 0) == (A | CL), "a click and a turn in the same frame give both");
    ml_button(&m, ML_LEFT, 0);

    /* In a menu or a tip the camera doesn't turn, and the turn that was owed is dropped. */
    ml_reset(&m);
    ml_motion(&m, 200, 0, 10);
    check(frames(3, 0, 1, 0, &all) == 3 && all == 0, "in a menu or a tip, motion doesn't press C");
    check(frames(3, 0, 0, 0, &all) == 3 && all == 0, "and it isn't kept for later");
}

/* Many made-up events: every frame's buttons stay among the four the mouse gives, never both C buttons. */
static void limits(void) {
    unsigned seed = 12345;
    unsigned short all = 0;
    int f, bad = 0;
    ml_reset(&m);
    for (f = 0; f < 100000; f++) {
        unsigned short b;
        seed = seed * 1103515245u + 12345u;
        switch ((seed >> 16) % 6) {
            case 0:
                ml_motion(&m, (int)((seed >> 8) % 2001) - 1000, (int)(seed % 7) - 3, (int)(seed % 50) - 5);
                break;
            case 1:
                ml_button(&m, (seed >> 4) & 1, (seed >> 5) & 1);
                break;
            case 2:
                ml_button(&m, (int)((seed >> 4) % 5) - 2, 1);
                break;
            case 3:
                if ((seed >> 7) % 50 == 0)
                    ml_reset(&m);
                break;
            default:
                break;
        }
        b = ml_step(&m, (seed >> 9) & 1, (seed >> 10) % 8 == 0);
        all |= b;
        if ((b & ~(A | B | CL | CR)) || ((b & CL) && (b & CR)))
            bad++;
    }
    check(bad == 0 && all == (A | B | CL | CR), "100000 frames of made-up events give only A, B, C-left and C-right, "
                                                "never both C buttons");
    check(ml_clamp_sensitivity(0) == ML_SENS_MIN && ml_clamp_sensitivity(1000) == ML_SENS_MAX
              && ml_clamp_sensitivity(17) == 17, "the sensitivity is kept from 1 to 40");
}

static void keys(void) {
    int x, y;
    ml_keys_stick(1, 0, 0, 0, &x, &y);
    check(x == 0 && y == 80, "W alone is the stick at full tilt up");
    ml_keys_stick(0, 0, 1, 0, &x, &y);
    check(x == -80 && y == 0, "A alone is full tilt left");
    ml_keys_stick(1, 0, 0, 1, &x, &y);
    check(x == 57 && y == 57, "W and D are a diagonal as long as a straight move (57, 57)");
    ml_keys_stick(0, 1, 1, 0, &x, &y);
    check(x == -57 && y == -57, "S and A are the other diagonal");
    ml_keys_stick(1, 1, 1, 0, &x, &y);
    check(x == -80 && y == 0, "W and S cancel, and A stays full tilt");
    ml_keys_stick(1, 1, 1, 1, &x, &y);
    check(x == 0 && y == 0, "all four keys are the stick at rest");
}

int main(void) {
    clicks();
    camera();
    limits();
    keys();
    printf("%s: %d failures\n", failures ? "FAILED" : "ok", failures);
    return failures != 0;
}
