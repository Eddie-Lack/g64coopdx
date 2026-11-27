/* Tests of the free camera (src/client/freecam.c), without the game: its place, and how it turns the stick.
 *
 * Usage: freecam_test. Exits with 1 if a test fails. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/freecam.h"

static int failures;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static int near(float a, float b) { return fabsf(a - b) < 0.5f; }

int main(void) {
    g64_freecam f;
    float game[3] = {0.0f, 100.0f, -300.0f}, target[3] = {0.0f, 0.0f, 0.0f}, out[3];
    uint16_t buttons;
    int8_t x, y;
    memset(&f, 0, sizeof f);

    /* Off: the game's camera stays and nothing is turned. */
    check(!g64_freecam_view(&f, game, target, 1, 1, out), "off: the game's camera stays");
    buttons = 0x0003, x = 10, y = 80;
    g64_freecam_input(&f, &buttons, &x, &y);
    check(buttons == 0x0003 && x == 10 && y == 80, "off: the input is as it came");

    /* On: the first view stands where the game's camera stands. */
    f.on = 1;
    check(g64_freecam_view(&f, game, target, 1, 1, out) && near(out[0], 0.0f) && near(out[1], 100.0f) &&
              near(out[2], -300.0f),
          "on: the first view stands where the game's camera stands");
    buttons = 0, x = 0, y = 80;
    g64_freecam_input(&f, &buttons, &x, &y);
    check(x == 0 && y == 80, "with the two cameras together, up stays up");

    /* Half a turn (1800 counts at sensitivity 10): the camera stands on the other side, and up is down for the game. */
    g64_freecam_motion(&f, 1800, 0, 10);
    check(g64_freecam_view(&f, game, target, 1, 1, out) && near(out[0], 0.0f) && near(out[1], 100.0f) &&
              near(out[2], 300.0f),
          "half a turn: the camera stands on the other side of the point");
    buttons = 0, x = 0, y = 80;
    g64_freecam_input(&f, &buttons, &x, &y);
    check(x == 0 && y == -80, "half a turn: up on the stick is down for the game's camera");

    /* A quarter turn the other way from the start: up becomes a side for the game. */
    g64_freecam_motion(&f, -2700, 0, 10);
    g64_freecam_view(&f, game, target, 1, 1, out);
    buttons = 0, x = 0, y = 80;
    g64_freecam_input(&f, &buttons, &x, &y);
    check(abs(x) == 80 && y == 0, "a quarter turn: up on the stick is a side for the game's camera");

    /* The d-pad, read as a full stick, is turned too, and its buttons go. C-left and C-right turn the camera and go;
       C-up stays. */
    buttons = 0x0800 | 0x0002 | 0x0008, x = 0, y = 0;
    g64_freecam_input(&f, &buttons, &x, &y);
    check(buttons == 0x0008 && abs(x) >= 78 && abs(y) <= 8, "the d-pad is turned, C-left goes and C-up stays");

    /* Mouse down raises the camera; with invertY it lowers it. */
    {
        float before;
        f.pitch = 0.0f;
        g64_freecam_motion(&f, 0, 100, 10);
        before = f.pitch;
        f.invertY = 1;
        f.pitch = 0.0f;
        g64_freecam_motion(&f, 0, 100, 10);
        check(before > 0.1f && f.pitch < -0.1f, "mouse down raises the camera, and lowers it with Y inverted");
        f.invertY = 0;
    }
    /* The height stays within its range. */
    g64_freecam_motion(&f, 0, 100000, 10);
    check(f.pitch <= G64_FC_PITCH_MAX && f.pitch >= G64_FC_PITCH_MIN, "the height stays within its range");

    /* Where it may not stand in (a menu, the look-around mode), the game's camera stays and the stick isn't turned. */
    check(!g64_freecam_view(&f, game, target, 0, 1, out), "where it may not stand in, the game's camera stays");
    buttons = 0x0001, x = 0, y = 80;
    g64_freecam_input(&f, &buttons, &x, &y);
    check(buttons == 0x0001 && x == 0 && y == 80, "then the input is as it came");

    printf("%s the free camera\n", failures ? "FAIL" : "ok  ");
    return failures ? 1 : 0;
}
