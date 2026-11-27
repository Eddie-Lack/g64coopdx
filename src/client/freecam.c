/* The free camera (freecam.h). */
#include "freecam.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

void g64_freecam_reset(g64_freecam *f) {
    int on = f->on, invertY = f->invertY;
    memset(f, 0, sizeof *f);
    f->on = on;
    f->invertY = invertY;
}

static float clamp_pitch(float p) {
    return p < G64_FC_PITCH_MIN ? G64_FC_PITCH_MIN : p > G64_FC_PITCH_MAX ? G64_FC_PITCH_MAX : p;
}

void g64_freecam_motion(g64_freecam *f, int dx, int dy, int sensitivity) {
    /* A tenth of a degree for each count at sensitivity 10. */
    float k = (float)(sensitivity < 1 ? 1 : sensitivity > 40 ? 40 : sensitivity) * (PI_F / 18000.0f);
    if (!f->have)
        return;
    /* The game's stick to the right goes to camera.rot.y less a quarter turn: a turn to the right lowers the yaw. */
    f->yaw -= (float)dx * k;
    f->yaw = fmodf(f->yaw, 2.0f * PI_F);
    f->pitch = clamp_pitch(f->pitch + (float)(f->invertY ? -dy : dy) * k);
}

int g64_freecam_view(g64_freecam *f, const float game[3], const float target[3], int allowed, int key, float out[3]) {
    float dx = game[0] - target[0], dy = game[1] - target[1], dz = game[2] - target[2];
    float flat = sqrtf(dx * dx + dz * dz), dist = sqrtf(dx * dx + dy * dy + dz * dz), h;
    /* The game's camera.rot.y: the direction from the camera to the point (camera.c, CalcCameraRot). */
    f->gameYaw = atan2f(dx, dz) + PI_F;
    f->active = 0;
    if (!f->on || !allowed || dist < 1.0f)
        return 0;
    if (!f->have || f->key != key) {
        f->yaw = f->gameYaw;
        f->pitch = clamp_pitch(atan2f(dy, flat));
        f->have = 1;
        f->key = key;
    }
    h = dist * cosf(f->pitch);
    out[0] = target[0] - sinf(f->yaw) * h;
    out[1] = target[1] + dist * sinf(f->pitch);
    out[2] = target[2] - cosf(f->yaw) * h;
    f->active = 1;
    return 1;
}

static int8_t clamp_axis(float v) {
    long r = lroundf(v);
    return (int8_t)(r < -80 ? -80 : r > 80 ? 80 : r);
}

void g64_freecam_input(g64_freecam *f, uint16_t *buttons, int8_t *x, int8_t *y) {
    float d, c, s, sx, sy;
    if (!f->active)
        return;
    if (*buttons & G64_FC_CRIGHT)
        f->yaw += G64_FC_C_TURN;
    if (*buttons & G64_FC_CLEFT)
        f->yaw -= G64_FC_C_TURN;
    *buttons = (uint16_t)(*buttons & ~(G64_FC_CLEFT | G64_FC_CRIGHT));
    sx = (float)*x;
    sy = (float)*y;
    /* The game reads a resting stick and the d-pad as a full stick (controller.c), and only in levels. */
    if (*x == 0 && *y == 0 && (*buttons & G64_FC_DPAD)) {
        if (*buttons & 0x0800)
            sy = 79.0f;
        else if (*buttons & 0x0400)
            sy = -79.0f;
        if (*buttons & 0x0200)
            sx = -79.0f;
        else if (*buttons & 0x0100)
            sx = 79.0f;
        *buttons = (uint16_t)(*buttons & ~G64_FC_DPAD);
    }
    if (sx == 0.0f && sy == 0.0f)
        return;
    /* The game moves the glove to camera.rot.y less the stick's angle, atan2(x, y). The stick's angle grows by the
       game's yaw less the free camera's: the glove then moves by the free camera's yaw. */
    d = f->gameYaw - f->yaw;
    c = cosf(d);
    s = sinf(d);
    *x = clamp_axis(sx * c + sy * s);
    *y = clamp_axis(-sx * s + sy * c);
}
