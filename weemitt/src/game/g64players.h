/* g64coopdx: more than one player in one game (g64players.c). */
#ifndef G64PLAYERS_H
#define G64PLAYERS_H

#define G64_MAX_PLAYERS 16

struct Platform;
struct Actor;
struct Bullet;

/* 1 in the online game (src/client/netplay.c). Each player then takes an exit alone, and leaves the game (the session
   moves them to the level the exit leads to); a level-wide exit takes every player. Set it before power-on. */
extern s32 g64Online;

/* The progress that a session shares, from the game's save slot: levels done, levels with all garibs, the hub's
   progress, and the worlds done (a bit per world). In an exit's record, raisedBit says why the exit raised hubTarget:
   the bit (plus 1) of the level whose first world exit raised it, G64_RAISED_CAVE for the first visit to the cave, or
   0. Two instances that each raise hubTarget from the same value report the same number; the host counts the reasons
   instead. */
#define G64_RAISED_CAVE 0xFF
typedef struct G64Save {
    u32 levels;
    u32 allGaribs;
    u8 hubTarget;
    u8 hubLevel;
    u8 worldComplete;
    u8 raisedBit;
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
#define G64_TEST_EXITS 8
extern G64TestExit g64TestExits[G64_TEST_EXITS];

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
void G64TipView(s32 player);
void G64CannonView(s32 player);
/* 1 for a proxy actor (another player's glove or ball). */
s32 G64IsProxy(struct Actor *a);
/* Each player's chosen glove and ball colors, by player number: G64_COLOR_SET plus 0xRRGGBB, or 0 for the color of
   the player's number (player 0 keeps the game's own). Set it before power-on: every machine has the same. */
#define G64_COLOR_SET 0x1000000u
extern u32 g64PlayerColor[G64_MAX_PLAYERS][2];
/* The color (0xRRGGBB) to tint an actor with, for a player's glove or ball; 0 for none. */
u32 G64ActorTint(struct Actor *a);
/* collect.c: the other players' runs of garibs taken in quick succession count down (g64players.c). */
void G64GaribRuns(void);
/* The texture words of another player's glove or ball that differ from the viewer's (g64players.c). */
s32 G64ActorTextures(struct Actor *a, u32 *from, u32 *to, s32 max);
/* A player's glove position, by number; 0 when the player is not in this game. */
s32 G64PlayerPosition(s32 player, f32 *x, f32 *y, f32 *z);
/* The same for a player's ball. */
s32 G64BallPosition(s32 player, f32 *x, f32 *y, f32 *z);
/* The cave's scene for each player online (game.c, DoCaveStuff), for the player in the globals: the frames since they
   came into the level, whether a crystal was due then, their flight's pedestal (a platform tag, 1 to 7), and when
   they came. G64CavePlayers runs G64CavePlayer for each player who is not in the globals. G64Arrive starts a player who
   came into a running level afresh. */
s32 G64CaveFramesHere(void);
s32 G64CaveDue(void);
s32 G64CaveTarget(void);
/* The highest pedestal of the flights of the players who are not in the globals, or 0. */
s32 G64CaveTargetsInAir(void);
void G64SetCaveTarget(s32 tag);
void G64SetCaveArrival(s32 frame, s32 due);
void G64CavePlayers(void);
void G64CavePlayer(void);
void G64Arrive(void);
/* Where a player hears from (the online game's sound, g64coopdx's src/client/sound.c): their camera's position and
   turn, and the level's sound distance (MAX_SFX_DIST). 0 when the player is not in this game. */
s32 G64SoundListener(s32 player, f32 *pos, f32 *yaw, s32 *maxDist);
/* For tests: the point a player's camera looks at; 0 when the player is not in this game. */
s32 G64CameraTarget(s32 player, f32 *x, f32 *y, f32 *z);
/* For tests: the position of the level's first platform with a tag; 0 when there is none. */
s32 G64PlatformPosition(s32 tag, f32 *x, f32 *y, f32 *z);
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

/* Enemies and bullets look at one player each (g64players.c, "Enemies and bullets"). G64EnemyBegin swaps that player
   into the globals before enemy x's update and returns their slot; G64EnemyEnd swaps them back. G64BulletBegin and
   G64BulletEnd do the same for a bullet. G64BulletMade and G64BulletGone keep the record of whom each bullet homes in
   on. G64EnemyNear: 1 when a player who is not in the globals holds the actor's ball, or their camera is within the
   range (squared) of it. */
s32 G64EnemyBegin(s32 enemy);
/* Inside an enemy's update, for code that is for every player (g64players.c): the number of players to go through
   (0 with one player), and a pair that puts player k in the globals in place of the enemy's target and back.
   G64OtherBegin returns 0 for the target and for an empty slot. */
s32 G64OtherPlayers(void);
s32 G64OtherBegin(s32 k);
void G64OtherEnd(s32 k);
/* Sets the camera's height above its target for every player. */
void G64SetLevelCamHeight(s32 height);
/* The first space boss's carrier, the player who brought their ball to the robot's platform. G64CarrierSet(1) notes
   the player in the globals, G64CarrierSet(0) that there is none. G64CarrierBegin puts the carrier in the globals: it returns a value for G64CarrierEnd, 0
   when nothing was swapped, or -1 when the carrier has left the game. */
void G64CarrierSet(s32 on);
s32 G64CarrierBegin(void);
void G64CarrierEnd(s32 k);
void G64EnemyEnd(s32 enemy, s32 slot);
/* After UpdateEnemies clears an enemy's hit flags: its record of who hit it goes. */
void G64EnemyHitsCleared(s32 enemy);
/* 1 when a bullet homes in on the player in the globals, or has no record (RemoveHandAndBallBullets: a player's
   respawn removes only the bullets aimed at them). */
s32 G64BulletMine(struct Bullet *b);
s32 G64BulletBegin(struct Bullet *b);
void G64BulletEnd(struct Bullet *b, s32 slot);
void G64BulletMade(struct Bullet *b);
void G64BulletGone(struct Bullet *b);
s32 G64EnemyNear(struct Actor *a, f32 rangeSq);
/* For tests: enemy i's position, and its type plus 1 while it is active; 0 when it is not, or there is none. */
s32 G64EnemyInfo(s32 i, f32 *x, f32 *y, f32 *z);
/* For tests: the number of bullets, and the positions (x, y, z) of the first max of them. */
s32 G64BulletPositions(f32 *xyz, s32 max);
/* For tests: the enemy that holds a player's ball, by number; -1 for none. */
s32 G64BallHolder(s32 player);

/* Puzzles and platforms (g64players.c). G64PuzzleSlots: how many slots a puzzle's condition on the player is tested
   in (1 with one player). G64PuzzleSlotBegin swaps slot i's player into the globals (0 when the slot is empty);
   G64PuzzleSlotEnd swaps them back. G64AnyBallOn and G64AnyGloveOn: 1 when the ball or glove of a player who is not
   in the globals stands on the platform. G64NearestBall and G64NearestGlove: the nearest player's ball or glove. */
s32 G64PuzzleSlots(void);
s32 G64PuzzleSlotBegin(s32 i);
void G64PuzzleSlotEnd(s32 i);
s32 G64AnyBallOn(struct Platform *p);
s32 G64AnyGloveOn(struct Platform *p);
struct Actor *G64NearestBall(const Vec3 *p);
struct Actor *G64NearestGlove(const Vec3 *p);

/* For tests: puzzle k's state (in the order CheckPuzzles runs them) and its condition j; 0 when there is none. A
   player's ball put at a position, at rest. */
s32 G64PuzzleInfo(s32 k, s32 *timesLeft, s32 *active, s32 *count);
s32 G64PuzzleCondition(s32 k, s32 j, s32 *type, s32 *tag, s32 *value);
void G64PlaceBall(s32 player, f32 x, f32 y, f32 z);
/* For tests: gives a player's ball a velocity. */
void G64PushBall(s32 player, f32 x, f32 y, f32 z);
/* For tests: when puzzle k is a camera zone (one "glove in a box" condition, camera actions only), its box: the
   corner (x, y, z) and the size (x, y, z). 0 otherwise. */
s32 G64PuzzleZone(s32 k, f32 *box);

/* Camera zones (puzzle.c, CheckPuzzles): G64PuzzleInSlot is 1 while another player is swapped in for the logic.
   G64PuzzleCameraState exchanges slot i's camera zones' state with the puzzles' (twice restores).
   PuzzleCameraStateSize and PuzzleSwapCameraState are puzzle.c's: the bytes of that state, and the exchange with a
   buffer (copy: only copy into it). */
s32 G64PuzzleInSlot(void);
void G64PuzzleCameraState(s32 i);
s32 PuzzleCameraStateSize(void);
void PuzzleSwapCameraState(u8 *buf, s32 copy);
/* puzzle.c: 1 for an action that only sets the camera (a camera zone's). */
s32 IsCameraZoneAction(s16 type);

/* Respawns (g64players.c, "Respawns"): 1 when each player respawns at their own checkpoint (online, or more than one
   player). The countdown, the camera's place and RestartPlayer's frame stamp k of the player in the globals for their
   checkpoint gate; offline with one player, the gate's own fields and own. G64CheckRestartGate: the gate's touch test
   for every player's ball. */
s32 G64PerPlayerRestart(void);
s16 *G64RestartTimer(struct Platform *gate);
Vec3 *G64RestartCamera(struct Platform *gate);
u32 *G64RestartStamp(s32 k, u32 *own);
/* For RestartPlayer's set-up: 1 when the respawn of the player in the globals follows a death, so it restarts no
   enemies; 0 offline with one player, and for the level's opening respawn (which it then marks as done). */
s32 G64RestartIsDeath(void);
/* 1 while any player's respawn runs (a crumbling platform comes back then, CrumblePlatform). */
s32 G64AnyRestarting(void);
void G64CheckRestartGate(struct Platform *platform);
/* For tests: a player's respawn countdown (65 at a death, 0 when none runs), by number; -1 when not in this game. */
s32 G64RestartCountdown(s32 player);
/* After CalculateShadows' own actors: the shadow altitude of every other player's glove, ball and fake ball. */
void G64SlotShadows(void);
/* For tests: the position of the k-th platform with a restart gate (0 when there is none); a player's checkpoint as
   that k (-1 for none, -2 when the player is not in this game); a player's lives. */
s32 G64GatePosition(s32 k, f32 *x, f32 *y, f32 *z);
s32 G64PlayerCheckpoint(s32 player);
s32 G64PlayerLives(s32 player);
/* For tests: the altitude of a player's glove above the ground under it (-1 for none, -2 when not in this game). */
f32 G64PlayerAltitude(s32 player);

/* World hazards (g64players.c): the number of slots to go through after the actor list (0 with one player, or while
   a slot is swapped in), and slot i's glove and ball (i from 1; NULL for an empty slot). They are read and changed in
   place. */
s32 G64SlotCount(void);
struct Actor *G64SlotGlove(s32 i);
struct Actor *G64SlotBall(s32 i);
/* A hazard that hit slot i's glove or ball: G64HazardBegin swaps that player into the globals (0 when it cannot),
   G64HazardEnd swaps them back. G64SlotBallBursting: 1 when slot i's ball is bursting. */
s32 G64HazardBegin(s32 i);
void G64HazardEnd(s32 i);
s32 G64SlotBallBursting(s32 i);
/* A bonus level ends for the player in the globals (g64players.c): 1 when they took the exit alone, online; 0 when
   the caller must end the level for everyone. */
s32 G64PlayerExits(s32 type, s32 complete);
/* 1 when a footprint is the slam ring of a player who is not in the globals (footprint.c). G64FootprintGone: a
   footprint is freed, and the players whose ring it was forget it. */
s32 G64IsPlayerFootprint(const void *footprint);
void G64FootprintGone(const void *footprint);
/* Quakes (quake.c). G64QuakeAt notes where a quake started, the two ranges its strength comes from, and that the
   player in the globals is the one whose distance set it (pos NULL: the quake has no place). G64QuakeStrength gives
   the quake's strength for the player in the globals at the point `blend` between its two ends: it leaves *str as it
   is for one player, for a quake without a place and for the player whose distance set it, and else works it out
   from that player's own distance. It returns 0 when the quake does not reach them. */
void G64QuakeAt(const struct Vec3 *pos, f32 maxStrength, f32 minStrength);
s32 G64QuakeStrength(f32 blend, f32 *str);
/* Cameos (g64players.c, "Cameos"). A cameo plays for the player who set it off, or for the first player.
   puzzle.c tells who met a puzzle's conditions (G64PuzzleMetReset, G64PuzzleMet, G64CameoStarter). The cameo's
   player is in the globals while it starts (G64CameoStartBegin) and steps (G64CameoSwapIn); G64CameoSwapOut puts
   the first player back. G64CameoView gives the other players the cameo's camera, G64CameoAfter holds them, and
   G64CameoHeld is 1 for them while it plays. */
void G64PuzzleMetReset(void);
void G64PuzzleMet(s32 i);
void G64CameoStarter(void);
void G64CameoHolding(void);
s32 G64CameoStartBegin(void);
s32 G64CameoSwapIn(void);
void G64CameoSwapOut(s32 i);
void G64CameoAfter(void);
void G64CameoView(void);
s32 G64CameoHeld(void);
/* Tips (g64players.c, "Tips"). Online a tip does not stop the level: each player reads it on their own, and only
   the reader is held. G64TipsOwn is 1 then. G64TipReaders gives the players (a bit each, by player number) whose
   glove is at a tip and who have just pressed B, G64TipEveryone all the players in the game. G64TipOpen opens the
   tip for those of them who read none, with the tip's sound, and returns 1 if it opened for any. G64TipsStep runs
   the open tips once a frame: A turns the page or closes. G64TipHeld is 1 while the player in the globals reads. */
s32 G64TipsOwn(void);
u32 G64TipReaders(const struct Vec3 *pos);
u32 G64TipEveryone(void);
s32 G64TipOpen(u32 players, s32 tag, const struct Vec3 *pos);
void G64TipsStep(void);
s32 G64TipHeld(void);
/* For the client's own tip box and for tests: the tip that a player reads. It returns the number of the page's
   lines and fills lines with them (at most max), or 0 when the player reads no tip. more is 1 when another page
   follows. The lines are in the game's memory: they hold the game's text and stay on this machine. */
s32 G64TipPage(s32 player, const char **lines, s32 max, s32 *more);
/* Any ball (g64players.c, "Any ball"): the host's choice that a glove may take any free ball. G64AnyBallStep
   runs once a frame after every player's part. G64BallLostElsewhere is 1 when the lost ball of the player in the
   globals is another player's, who dies for it. */
extern s32 g64AnyBall;
/* A level pack's level file (g64players.c, "A level pack"). */
extern s32 g64PackLevel;
extern u32 g64PackRom, g64PackSize;
void G64SetPack(s32 level, u32 rom, u32 size);
/* A level pack's own models (g64packbanks.c): where its texture bank and its object bank are in the ROM buffer.
   G64LoadPackBanks loads them into free slots when the level is the pack's (LoadLevel, game.c). */
extern u32 g64PackTexRom, g64PackTexSize, g64PackObjRom, g64PackObjSize;
extern s32 g64PackHeapFree, g64PackHeapGap, g64PackHeapBlocks, g64PackHeapLastStatic;
void G64SetPackModels(u32 texRom, u32 texSize, u32 objRom, u32 objSize);
void G64LoadPackBanks(s32 num);
/* The object banks LoadLevel loads for a level before the pack's (g64packbanks.c). */
s32 G64LevelBankNumbers(s32 num, u32 *banks, s32 max);
/* For tests (g64packbanks.c): the ids in an object slot, and the ids of the objects the game finds by its own names. */
s32 G64ObjectSlotIds(s32 slot, u32 *ids, s32 max);
s32 G64GameObjectIds(u32 *ids, s32 max);
const void *G64PackBankLoaded(s32 objects, u32 *word);
/* The collision planes of the last level start (parse.c, G64PlanesFit): the blocks, bytes and work they would take.
   Also the last static block's number before them. For tests. */
extern s32 g64PlanesBlocks, g64PlanesLastStatic;
extern s64 g64PlanesBytes, g64PlanesWork;
/* One world at a time (g64players.c, "One world at a time"): the host's rule that a portal from the hub to another
   world is shut while players are in a world. */
extern s32 g64OneWorld;
void G64SetSessionWorld(s32 world);
s32 G64SessionWorld(void);
s32 G64PortalShut(s32 player);
void G64AnyBallStep(void);
s32 G64BallLostElsewhere(void);
/* debris.c: a piece of debris is freed, and no player who is not in the globals keeps it as their fake ball. */
void G64FakeballGone(const void *debris);
/* DisableControl(time) for every player who is not in the globals. */
void G64DisableControlAll(s32 time);
/* Water (water.c): the key of an actor's water events. With a slot swapped in for a hazard, its glove and ball are
   keyed by the slot's own actors, not by the globals' addresses. G64SlotWaterReset clears the other players'
   water state at the start of a frame. */
struct Actor *G64EventKey(struct Actor *a);
void G64SlotWaterReset(void);
/* For tests: the place of the k-th hazard of a kind (0 a wind's middle, 1 a water's middle at its surface, 2 a
   spike, 3 a buzzer's middle: 2 when it is buzzing); 0 when there is none. A player's health, their glove's
   velocity, and their glove's water state (-1 when not in this game). */
s32 G64HazardPlace(s32 kind, s32 k, f32 *x, f32 *y, f32 *z);
s32 G64PlayerHealth(s32 player);
s32 G64PlayerVelocity(s32 player, f32 *x, f32 *y, f32 *z);
s32 G64PlayerInWater(s32 player);

/* Garibs and pick-ups (collect.c). G64GaribGroupNear: 1 when another player's glove or ball is within the radius of
   a group of garibs. G64GaribTaker: the slot of the player who takes the garib at pos, the one whose glove or ball
   reaches it from the nearest; 0 for the first player or nobody. G64GaribGained adds a garib to every other player's
   count: the count is the team's. G64PickupTaker: the first slot whose glove is within the pick-up's radius; -1 when
   there is none. */
s32 G64GaribGroupNear(const struct Vec3 *pos, f32 radius);
s32 G64GaribTaker(const struct Vec3 *pos);
void G64GaribGained(void);

/* For tests of the level file check (the repository's src/client/levcheck.c): while g64ParseLogOn is set, the level
   parser records each token it reads, with the token's offset in the level file. The record starts again with each
   level. Entry k is g64ParseLog[k]: the offset, then the token. */
#define G64_PARSE_LOG 8192
extern s32 g64ParseLogOn, g64ParseLogCount;
extern s32 g64ParseLog[G64_PARSE_LOG][2];
void G64ParseTrace(s32 offset, s32 token);
/* For the same tests: while g64ParseLogOn is set, FindObject records each id it is asked for while the parser reads
   a token, with the index of that token in g64ParseLog. The record starts again with the parser's. Entry k is
   g64FindLog[k]: the token's index, then the id. */
extern s32 g64FindLogCount;
extern u32 g64FindLog[G64_PARSE_LOG][2];
void G64FindObjectTrace(u32 id);

/* Marks the level as one that cannot be played: the game's own state for a level without its file. The level
   parser and the code that makes enemies and pick-ups call it where a level file names something the game does not
   have. Online, the level's players then go back to the hub. */
void G64Unplayable(const char *what);

/* Tells a level pack's script that something happened to the player whose state is in the game's variables now:
   1 a garib taken, 2 a death (src/port/g64lua.c, G64LuaEvent). The script hears of it at the next frame. */
void G64ScriptEvent(s32 kind);
s32 G64PickupTaker(const struct Vec3 *pos, f32 radiusSq);
/* G64PickupTaken notes that the player in the globals took this pick-up. G64PickupScorer gives the slot of the
   player who took it, for its points ten frames later, and forgets the note; 0 for the first player or nobody. */
void G64PickupTaken(const void *collectable);
s32 G64PickupScorer(const void *collectable);
/* 1 while a slot's player is in the globals for their part of the frame and handControl.orbiter3 is the world's. */
s32 G64WorldOrbiterDone(void);
/* 1 with more than one player: a power-up's orbiters then stay out of the actor list, and each player's are drawn
   as proxies (g64players.c). */
s32 G64OrbitersByProxy(void);
/* For tests: a player's count of garibs and their time left on a power-up (-1 when not in this game), the number of
   garibs left in the level and the place of the k-th of them, and the place and type of the k-th pick-up. */
s32 G64PlayerGaribs(s32 player);
s32 G64PlayerPowerup(s32 player, s32 type);
s32 G64PlayerScore(s32 player);
s32 G64GaribPlace(s32 k, f32 *x, f32 *y, f32 *z);
s32 G64PickupPlace(s32 k, f32 *x, f32 *y, f32 *z);

#endif
