/* g64coopdx: more than one player in one game (g64players.c). */
#ifndef G64PLAYERS_H
#define G64PLAYERS_H

#define G64_MAX_PLAYERS 16

struct Platform;

/* 1 in the online game (src/client/netplay.c). Each player then takes an exit alone, and leaves the game (the session
   moves them to the level the exit leads to); a level-wide exit takes every player. Set it before power-on. */
extern s32 g64Online;

/* The progress that a session shares, from the game's save slot: levels done, levels with all garibs, the hub's
   progress, and the worlds done (a bit per world). */
typedef struct G64Save {
    u32 levels;
    u32 allGaribs;
    u8 hubTarget;
    u8 hubLevel;
    u8 worldComplete;
    u8 pad;
} G64Save;

/* An exit taken in this frame: who, by which exit type (enum ExitType), whether it completed the level, the level and
   world it leads to, and the progress after it (the save as the level leaves it, with the exit's own effects). */
typedef struct G64ExitRecord {
    s32 player;
    s32 type;
    s32 complete;
    s32 level;
    s32 world;
    G64Save save;
} G64ExitRecord;
/* For tests: a player who takes an exit at a set frame of a level (the frames counted from G64StartPlayers), as if
   they had reached an exit platform. level -1: unused. Set it before power-on: every copy of the game then has it. */
typedef struct G64TestExit {
    s32 level;
    s32 frame;
    s32 player;
    s32 type;
} G64TestExit;
extern G64TestExit g64TestExits[4];

/* The exits taken in the last frame, in the order they were taken. G64SetPresent clears them before each frame. */
extern G64ExitRecord g64Exits[G64_MAX_PLAYERS];
extern s32 g64ExitCount;

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
/* Each player's chosen glove and ball colors, by player number: G64_COLOR_SET plus 0xRRGGBB, or 0 for the color of
   the player's number (player 0 keeps the game's own). Set it before power-on: every machine has the same. */
#define G64_COLOR_SET 0x1000000u
extern u32 g64PlayerColor[G64_MAX_PLAYERS][2];
/* The color (0xRRGGBB) to tint an actor with, for a player's glove or ball; 0 for none. */
u32 G64ActorTint(struct Actor *a);
/* A player's glove position, by number; 0 when the player is not in this game. */
s32 G64PlayerPosition(s32 player, f32 *x, f32 *y, f32 *z);
/* Moves a player's glove and ball, by number. For drawing only: the online program shifts the other players on a
   state it loads again after the draw. */
void G64NudgePlayer(s32 player, f32 dx, f32 dy, f32 dz);
/* A player's own pause menu (src/client/netplay.c) sends a choice as an input: the reserved button G64_ACTION_BUTTON,
   which no controller has, with the action in the stick's x. The game carries it out for that player alone. */
#define G64_ACTION_BUTTON 0x0080
#define G64_ACTION_CHECKPOINT 1 /* the glove, ball and camera go back to the player's last checkpoint */
#define G64_ACTION_HUB 2        /* the player leaves for the hub, as by an exit */
void G64PlayerAction(s32 player, s32 action);
/* 1 when the player with this number is in this game. */
s32 G64HasPlayer(s32 player);
/* Between two frames: a player joins (a copy of the player in the globals: beside it before the level's first frame
   of play, at the level's start after it) or leaves. When the player in the globals leaves, the player in slot 1
   takes the globals. The last player cannot leave. */
void G64AddPlayer(s32 player);
void G64RemovePlayer(s32 player);
/* The players in a game, as a bit per player number, from the session's confirmed inputs. G64StartPlayers sets them
   before a level starts, in order of number. G64SetPresent, before each frame, adds a player whose bit has come on
   since the last frame and removes one whose bit has gone off. Only a change counts: a player who left by an exit
   stays out while their bit is still on. */
void G64StartPlayers(u32 present);
/* Online: for an exit platform (CheckForExit), every player whose glove or ball reaches it takes it. */
void G64CheckExit(struct Platform *platform);
/* Online: a level-wide exit (ExitLevel) takes every player in the game. */
void G64EveryoneExits(s32 type, s32 complete);
/* The start record of a level built for the online game: its world and the session's progress. port_game_start_level
   applies it once, after StartNewGame and before the level starts (G64ApplyStartRecord). */
void G64SetStartRecord(s32 world, const G64Save *save);
void G64ApplyStartRecord(void);
/* For tests: the position of the first exit platform of a type (1), or 0 when the level has none; and a player's glove
   put at a position. */
s32 G64FindExit(s32 type, f32 *x, f32 *y, f32 *z);
void G64PlacePlayer(s32 player, f32 x, f32 y, f32 z);
void G64SetPresent(u32 present);

#endif
