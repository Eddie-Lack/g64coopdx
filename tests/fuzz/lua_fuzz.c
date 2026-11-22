/* Fuzz target for the scripts' sandbox: the input is a script's text. The script loads and runs for a few frames.
   It must end within the limit on work, whatever it does, and must not reach memory outside the scripts' block. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int G64LuaLoad(const char *source, unsigned int size);
void G64LuaFrame(void);
void G64LuaReset(void);

/* The game's side of the scripts' calls: one player at a place that the script can move. */
static int frames;
static float place[3];

int G64PlayerPosition(int p, float *x, float *y, float *z) {
    if (p != 0) {
        return 0;
    }
    *x = place[0];
    *y = place[1];
    *z = place[2];
    return 1;
}

void G64PlacePlayer(int p, float x, float y, float z) {
    if (p == 0) {
        place[0] = x;
        place[1] = y;
        place[2] = z;
    }
}

int G64PlayerInLevel(int p) {
    return p == 0 || (p == 1 && frames == 1);
}

int G64BallPosition(int p, float *x, float *y, float *z) {
    if (p != 0) {
        return 0;
    }
    *x = place[0] + 1;
    *y = place[1] + 2;
    *z = place[2] + 3;
    return 1;
}

int G64PlayerBallType(int p) {
    return p == 0 ? 2 : -1;
}

int G64PlayerGaribs(int p) {
    return p == 0 ? frames / 10 : -1;
}

int G64PlayerHealth(int p) {
    return p == 0 ? 3 : -100;
}

int G64PlayerLives(int p) {
    return p == 0 ? 5 : -100;
}

/* The game's sine and cosine take their constants from the ROM. These stand in for them here; the test with the
   real game compares the real ones between Linux and Windows (tests/rom/instances.c). */
float port_sinf(float x) {
    return x * 0.5f;
}

float port_cosf(float x) {
    return 1.0f - x * 0.25f;
}

int G64FramesRun(void) {
    return frames;
}

int G64Level(void) {
    return 7;
}

void port_log(const char *fmt, ...) {
    (void)fmt;
}

void port_dma(void *dst, unsigned int devAddr, unsigned int size) {
    (void)devAddr;
    memset(dst, 0, size);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    place[0] = place[1] = place[2] = 0;
    frames = 0;
    if (size == 0 || memchr(data, 0, size) != NULL) {
        return 0;
    }
    G64LuaLoad((const char *)data, (unsigned int)size);
    for (frames = 0; frames < 3; frames++) {
        G64LuaFrame();
    }
    G64LuaReset();
    return 0;
}
