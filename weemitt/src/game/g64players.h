/* g64coopdx: more than one player in one game (g64players.c). */
#ifndef G64PLAYERS_H
#define G64PLAYERS_H

#define G64_MAX_PLAYERS 16

/* The number of players in this game, 1 to G64_MAX_PLAYERS. Set it before a level starts. */
extern s32 g64Players;
/* Each player's number in the session, by place in this game: g64Member[0] is the player in the globals, the others
   are the slots. One machine and the next give the same player the same number, whichever game it is in. */
extern s32 g64Member[G64_MAX_PLAYERS];
/* The controller of every player but the one in the globals, for the next frame, by the player's number. The player
   in the globals reads the normal controller. */
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
/* Swaps a player's slot with the globals, by the player's number (twice restores). For drawing that player's view:
   the globals then hold the player, and the proxies show the others. Nothing for the player in the globals or one
   who is not in this game. */
void G64SwapPlayer(s32 player);
/* 1 for a proxy actor (another player's glove or ball). */
s32 G64IsProxy(struct Actor *a);
/* The color (0xRRGGBB) to tint an actor with, when it shows a player other than number 0; 0 for none. */
u32 G64ActorTint(struct Actor *a);
/* A player's glove position, by number; 0 when the player is not in this game. */
s32 G64PlayerPosition(s32 player, f32 *x, f32 *y, f32 *z);
/* 1 when the player with this number is in this game. */
s32 G64HasPlayer(s32 player);
/* Between two frames: a player joins (a copy of the player in the globals, beside it) or leaves. When the player in
   the globals leaves, the player in slot 1 takes the globals. The last player cannot leave. */
void G64AddPlayer(s32 player);
void G64RemovePlayer(s32 player);
/* The players in a game, as a bit per player number, from the session's confirmed inputs. G64StartPlayers sets them
   before a level starts, in order of number. G64SetPresent, before each frame, adds a player whose bit has come on
   since the last frame and removes one whose bit has gone off. Only a change counts: a player who left by an exit
   stays out while their bit is still on. */
void G64StartPlayers(u32 present);
void G64SetPresent(u32 present);

#endif
