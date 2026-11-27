/* g64coopdx: the free camera's hold on a player's view (src/client/freecam.h). The game's camera keeps running as it
   is: the logic reads it, and every machine has the same. Only the picture of a player's view comes from another
   place. The client calls these after G64SwapPlayer and before it draws the view, and loads the game's state again
   after the view. What G64SetViewCamera writes therefore never reaches the game. */
PORT_C_BEGIN
#include "common.h"
#include "actor.h"
#include "camera.h"
#include "enums.h"
#include "game.h"
#include "g64players.h"

extern s32 controlMethod;
extern u8 pauseMode;

/* The game's camera of the view about to be drawn: its place and the point it looks at. Returns 1 when a free camera
   may stand in for it: in a level that is played, with the game's pause menu closed, while the player steers the
   glove or the ball and the game's camera follows them. In the look-around mode (C-up), in the menus, in the game's
   scenes (cameraInfo.mode other than 1) and while a level holds the camera on something (overrideTimer) the game's
   camera stays. */
s32 G64ViewCamera(f32 pos[3], f32 target[3]) {
    pos[0] = camera.pos.x;
    pos[1] = camera.pos.y;
    pos[2] = camera.pos.z;
    target[0] = cameraInfo.actor.pos.x;
    target[1] = cameraInfo.actor.pos.y;
    target[2] = cameraInfo.actor.pos.z;
    return G64MouseInPlay() && (controlMethod == CONTROL_HAND || controlMethod == CONTROL_BALL) &&
           cameraInfo.mode == 1 && cameraInfo.overrideTimer == 0 && !gameInfo.cameoActive;
}

/* The view's camera stands at pos and looks at the game camera's point. GraphicsStep makes the view from the two, and
   the camera's angles from them (CalcCameraRot). For the view only. */
void G64SetViewCamera(const f32 pos[3]) {
    camera.pos.x = pos[0];
    camera.pos.y = pos[1];
    camera.pos.z = pos[2];
}

PORT_C_END
