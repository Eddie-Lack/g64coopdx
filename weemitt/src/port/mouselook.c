/* g64coopdx: the mouse as part of an N64 controller (mouselook.h).

   The clicks follow the game's own buttons (hand.c, ConditionsMet). Without the ball, A jumps and a held B points.
   With the ball, a B tap bounces it and a held B aims a throw; an A tap lobs it and a held A winds up a slap that
   lands when A is let go. The left click is the jump and the bounce: A without the ball, B with it. The right click
   is the point, the lob and the slap: B without the ball, A with it.

   The game lobs for an A tap of up to three frames, which a quick click can outlast. So a right click with the ball
   waits: let go within ML_LOB_FRAMES and the game gets ML_LOB_TAP frames of A. Held longer, it gets A until the click ends,
   and for at least ML_SLAP_FRAMES: the game winds up after three frames of A.

   The camera turns with the C buttons, as the controller turns it (camera.c, PlayerSpinCamera). The game then keeps
   its own collision and walls stop the camera. A held C button turns it at the game's own speed; the mouse presses
   the button in some frames and not others, so slower motion gives a slower turn. C-right moves the camera to its
   right, which turns the view to the left. */
#include "mouselook.h"

void ml_reset(MouseLook *m) {
    int b;
    for (b = 0; b < 2; b++) {
        m->down[b] = 0;
        m->clicked[b] = 0;
        m->as[b] = 0;
        m->frames[b] = 0;
        m->minFrames[b] = 0;
    }
    m->lobWait = 0;
    m->tapLeft = 0;
    m->turn = 0;
}

int ml_clamp_sensitivity(int s) {
    return s < ML_SENS_MIN ? ML_SENS_MIN : s > ML_SENS_MAX ? ML_SENS_MAX : s;
}

void ml_motion(MouseLook *m, int dx, int dy, int sensitivity) {
    long cap = (long)ML_TURN_CAP * ML_UNIT;
    (void)dy;
    /* A single event can't owe more than the cap, so the sum can't overflow. */
    if (dx > ML_TURN_CAP * ML_UNIT)
        dx = ML_TURN_CAP * ML_UNIT;
    if (dx < -ML_TURN_CAP * ML_UNIT)
        dx = -ML_TURN_CAP * ML_UNIT;
    m->turn += (long)dx * ml_clamp_sensitivity(sensitivity);
    if (m->turn > cap)
        m->turn = cap;
    if (m->turn < -cap)
        m->turn = -cap;
}

void ml_button(MouseLook *m, int button, int down) {
    if (button != ML_LEFT && button != ML_RIGHT)
        return;
    m->down[button] = down != 0;
    if (down)
        m->clicked[button] = 1;
}

unsigned short ml_step(MouseLook *m, int holdsBall, int plain) {
    unsigned short out = 0;
    int b;

    for (b = 0; b < 2; b++) {
        int active = m->down[b] || m->clicked[b];
        m->clicked[b] = 0;
        if (!m->as[b] && active) {
            /* The click's button is chosen once, when it starts. A change in what the glove holds while the button
               is down doesn't move the press to the other button. */
            if (plain || !holdsBall) {
                m->as[b] = b == ML_LEFT ? ML_BTN_A : ML_BTN_B;
            } else {
                m->as[b] = b == ML_LEFT ? ML_BTN_B : ML_BTN_A;
                if (b == ML_RIGHT)
                    m->lobWait = 1;
            }
            m->frames[b] = 0;
            m->minFrames[b] = 0;
        }
        if (!m->as[b])
            continue;
        if (b == ML_RIGHT && m->lobWait) {
            if (!m->down[b]) {
                /* Let go in time: a lob. */
                m->lobWait = 0;
                m->as[b] = 0;
                m->tapLeft = ML_LOB_TAP;
            } else if (++m->frames[b] >= ML_LOB_FRAMES) {
                /* Still held: a slap. A is held from now until the click ends, and for at least ML_SLAP_FRAMES, which
                   the game takes as a wind-up and not as a lob. */
                m->lobWait = 0;
                m->frames[b] = 1;
                m->minFrames[b] = ML_SLAP_FRAMES;
                out |= m->as[b];
            }
            continue;
        }
        if (!active && m->frames[b] >= m->minFrames[b]) {
            m->as[b] = 0;
            continue;
        }
        m->frames[b]++;
        out |= m->as[b];
    }
    if (m->tapLeft > 0) {
        m->tapLeft--;
        out |= ML_BTN_A;
    }

    if (plain) {
        m->turn = 0;
    } else if (m->turn > ML_UNIT / 2) {
        m->turn -= ML_UNIT;
        out |= ML_BTN_CLEFT;
    } else if (m->turn < -(ML_UNIT / 2)) {
        m->turn += ML_UNIT;
        out |= ML_BTN_CRIGHT;
    }
    return out;
}

void ml_keys_stick(int up, int down, int left, int right, int *x, int *y) {
    int dx = (right ? 1 : 0) - (left ? 1 : 0);
    int dy = (up ? 1 : 0) - (down ? 1 : 0);
    int v = dx && dy ? ML_STICK_DIAG : ML_STICK_FULL;
    *x = dx * v;
    *y = dy * v;
}
