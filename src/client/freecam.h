/* The free camera: a camera of this machine only, which the mouse turns all the way round the glove. The game's own
 * camera keeps running underneath, unchanged, and the logic reads only that one. The free camera changes two things:
 *   - The picture of this player's view. The client draws it from a copy of the game's state (netplay.c), so the
 *     camera written there never reaches the game.
 *   - The stick. Glover reads the stick by the angle of its own camera (hand.c, ProcessInput). The stick is turned by
 *     the angle between the free camera and the game's camera before it becomes this player's input. Up then moves
 *     the glove away from the free camera. The turned stick is a normal input, which the rollback core carries.
 * While the free camera is on, the C-left and C-right buttons turn it, and the game doesn't get them. C-up and C-down
 * still reach the game: C-up's look-around mode shows the game's camera.
 *
 * The free camera stands as far from the point the game's camera looks at as the game's camera does. Its own angles
 * are a turn round that point (yaw, in the convention of the game's camera.rot.y) and a height (pitch, 0 level with
 * the point, up to 1.3 above it and down to -0.35 below it, in radians). It has no collision: it can stand in a wall.
 *
 * Plain C. Nothing it computes enters the game's state except through the stick and the buttons. */
#ifndef G64_FREECAM_H
#define G64_FREECAM_H

#include <stdint.h>

typedef struct {
    int on;        /* the player's setting: the free camera is used */
    int invertY;   /* the player's setting: mouse up looks down */
    int have;      /* the angles are set (from the game's camera at the first view of a game) */
    int active;    /* the last view used the free camera: the stick is turned */
    int key;       /* the game the angles belong to; another one sets them again */
    float yaw, pitch;
    float gameYaw; /* the game's camera's camera.rot.y at the last view */
} g64_freecam;

/* The N64 buttons it reads. */
#define G64_FC_CRIGHT 0x0001
#define G64_FC_CLEFT 0x0002
#define G64_FC_DPAD 0x0F00
#define G64_FC_PITCH_MIN (-0.35f)
#define G64_FC_PITCH_MAX 1.3f
/* The turn of a held C button in one game frame, in radians (about 5 degrees). */
#define G64_FC_C_TURN 0.09f

void g64_freecam_reset(g64_freecam *f);
/* Mouse motion while the window holds the mouse: dx to the right turns the view right, dy down looks down (up with
 * invertY). sensitivity is the OPTIONS value, 1 to 40: at 10 a count is a tenth of a degree. */
void g64_freecam_motion(g64_freecam *f, int dx, int dy, int sensitivity);
/* The view of one drawn frame. game and target are the game's camera's place and the point it looks at, allowed
 * says whether a free camera may stand in (G64ViewCamera), and key names the game (its instance). Writes the free
 * camera's place to out and returns 1, or returns 0 when the game's camera stays. */
int g64_freecam_view(g64_freecam *f, const float game[3], const float target[3], int allowed, int key, float out[3]);
/* One game frame of input: the C-left and C-right buttons turn the camera and leave *buttons, and the stick (or the
 * d-pad, which the game reads as a full stick when the stick rests) is turned by the angle between the two cameras.
 * Does nothing while the last view used the game's camera. */
void g64_freecam_input(g64_freecam *f, uint16_t *buttons, int8_t *x, int8_t *y);

#endif
