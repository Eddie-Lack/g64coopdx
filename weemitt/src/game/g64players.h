/* g64coopdx: more than one player in one game (g64players.c). */
#ifndef G64PLAYERS_H
#define G64PLAYERS_H

#define G64_MAX_PLAYERS 16

/* The number of players, 1 to G64_MAX_PLAYERS. Set it before a level starts. */
extern s32 g64Players;
/* The controller of each player after the first, for the next frame. Player 0 reads the normal controller. */
extern u16 g64Buttons[G64_MAX_PLAYERS];
extern s8 g64StickX[G64_MAX_PLAYERS];
extern s8 g64StickY[G64_MAX_PLAYERS];

/* The extra players' slots, as copies of the first player. G64ExtraPlayersStep calls it in a level's first frame of
   play. */
void G64ExtraPlayersStart(void);
/* At a level's end, before the actors are freed (FreeLevel2). */
void G64ExtraPlayersFree(void);
/* In MainGameLoop, after the first player's part of the frame: the extra players' parts. */
void G64ExtraPlayersStep(void);
/* Before a draw: the proxies take the gloves and balls of the players who are not in the globals. */
void G64UpdateProxies(void);
/* Swaps player p's slot with the globals (twice restores). For drawing player p's view: the globals then hold
   player p, and the proxies show the others. Nothing for player 0 or a player without a slot. */
void G64SwapPlayer(s32 p);
/* A player's glove position; 0 when there is no such player. */
s32 G64PlayerPosition(s32 p, f32 *x, f32 *y, f32 *z);

#endif
