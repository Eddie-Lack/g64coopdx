/* g64coopdx: the mouse as part of an N64 controller (mouselook.c). The window loop gives it the mouse's motion and
   clicks. Once per game frame it gives back controller buttons: A or B for the clicks, and the C buttons that turn
   the camera. The game gets nothing else from the mouse. The online game carries these buttons as normal input.

   It's plain C with no SDL and no floats: the same events give the same buttons on every machine. */
#ifndef PORT_MOUSELOOK_H
#define PORT_MOUSELOOK_H

#ifdef __cplusplus
extern "C" {
#endif

/* The N64 buttons the mouse gives (the game's controller word). */
#define ML_BTN_A 0x8000
#define ML_BTN_B 0x4000
#define ML_BTN_CLEFT 0x0002
#define ML_BTN_CRIGHT 0x0001

/* The camera's sensitivity on the start screen's OPTIONS page. */
#define ML_SENS_MIN 1
#define ML_SENS_MAX 40
#define ML_SENS_DEFAULT 10
/* One game frame of a held C button is ML_UNIT. Each count of mouse motion adds the sensitivity, so at 10 the camera
   turns one frame's worth for every 84 counts. A held C button turns the camera about 8.4 degrees a frame. */
#define ML_UNIT 840
/* The most turn the mouse can owe the camera, in frames of a C button. More motion than this is dropped. */
#define ML_TURN_CAP 3
/* A right click while the glove holds the ball: let go within this many game frames and it's a lob (two frames
   of A). Held longer, A is held from then until the click ends: the glove winds up and slaps on the release. */
#define ML_LOB_FRAMES 4
/* The fewest frames of A for a slap: the game winds up (HAND_PREWHACK) once A has been held for three. */
#define ML_SLAP_FRAMES 4
/* The frames of A for a lob. The game lobs for up to three; two still lob when one of them comes too late online. */
#define ML_LOB_TAP 2
/* The stick's value for a held direction key, and for each axis of a diagonal (80 / sqrt 2). */
#define ML_STICK_FULL 80
#define ML_STICK_DIAG 57

enum { ML_LEFT, ML_RIGHT };

typedef struct MouseLook {
    int down[2];      /* the mouse button is down now */
    int clicked[2];   /* it went down since the last step: a click shorter than a frame still counts */
    int as[2];        /* the pad button the click gives, chosen when the step first sees it (0: none yet) */
    int frames[2];    /* game frames the click has lasted, or has sent its button */
    int minFrames[2]; /* the click sends its button for at least this many frames */
    int lobWait;      /* the right click waits to see whether it's a lob (as is A, nothing is sent yet) */
    int tapLeft;      /* frames of A still to send for a lob */
    long turn;        /* the turn owed, in ML_UNIT a frame; above 0 turns the view right */
} MouseLook;

/* Lets go of everything: the clicks end and the owed turn is dropped. Call it when the mouse is released. */
void ml_reset(MouseLook *m);
/* Mouse motion while the mouse is held by the window. dx is to the right. Up and down do nothing. */
void ml_motion(MouseLook *m, int dx, int dy, int sensitivity);
/* A mouse button (ML_LEFT or ML_RIGHT) went down or up. */
void ml_button(MouseLook *m, int button, int down);
/* One game frame: the buttons the mouse holds in it. holdsBall says whether the player's glove holds the ball; a
   click takes its button from it when the click starts. plain is 1 while a menu or a tip of the game is open: left
   is A, right is B and the camera doesn't turn. The result has only A, B, C-left and C-right, and never both C
   buttons. */
unsigned short ml_step(MouseLook *m, int holdsBall, int plain);
/* The stick for the direction keys that are held: full tilt, and a diagonal as long as a straight move. Opposite
   keys cancel. */
void ml_keys_stick(int up, int down, int left, int right, int *x, int *y);
/* The sensitivity kept in range. */
int ml_clamp_sensitivity(int s);

#ifdef __cplusplus
}
#endif

#endif
