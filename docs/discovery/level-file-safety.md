# What a level file must not do

The game's level parser (`weemitt/src/game/parse.c`) is the original game's. It trusts the level file. This file
lists what a level file must satisfy for that parser, and the code that uses what it parsed, to stay inside its
memory. It comes from a read of the code on 22 November 2026; nothing here was tested with a hostile file. Line
numbers are of that day. `P:` means `weemitt/src/game/parse.c`.

It exists for one decision: whether g64coopdx may load a level pack that comes from another player
([level-pack.md](level-pack.md)). A pack comes from a folder the player chose, or from the host after the player said yes. A pack from the host must pass the check below.

## The short answer

A check that makes a hostile level file safe must walk the file exactly as the parser does, hold about a hundred
rules on values and order, and know which objects, sub-objects, meshes and animations the borrowed level's banks
hold in the player's own ROM. Parts of the game that use parsed values were not read (the last section). Until such
a check exists and has been fuzzed against the real parser, a level file from a stranger can crash the game or
write outside its memory.

## What is checked today

`src/client/levcheck.c` checks a level file's structure. It walks the file as the parser does and holds it to these
rules:

- the file starts with its own size, and its name ends with a zero byte
- every token is one the parser handles, and the end token comes inside the file
- every token's fields lie inside the file
- a token that uses a platform, a puzzle, an enemy, a vent, a landscape piece, a group of garibs, a cameo or a
  buzzer comes after the token that makes one, in this file
- no token takes a short form (the tokens whose length depends on what came before, below)
- a ball start's place, read as tokens, holds no token
- the game's lists of a fixed size are not overrun: 30 enemies, the 30, 10 and 30 instructions of one enemy, 6
  patterns of a vent or a buzzer, 20 instructions of a cameo, 10 rope bridges of 1 to 100 planks
- an axis is 0, 1 or 2 (SPIN, PENDULUM, SEESAW, TOPPLE, ORBIT, a rope bridge)
- an enemy's type is 0 to 58, a garib's 0 to 3, a pick-up's 0 to 14, a sound's number 0 to 419
- a STARTPOINT names a point the platform's path has
- a puzzle condition (27 to 30) and a puzzle action (56 to 58) name only the level's 100 variables, in both of
  their operands
- a puzzle condition's number is 9 to 44 and an action's 45 to 87; a confine condition names limit 0 to 5
- a TIP's number is 0 to 32

Every rule passes on all 48 levels of the ROM. Of "The most dangerous" below, these cover points 1, 2 and 3 but for
a cameo's extra camera spheres, and the plank count of point 9.

How it is proven: the game's parser records each token it reads (`G64ParseTrace`, for tests only), and
`tests/rom/levcheck.c` compares that record with the check's walk on all 48 levels of the ROM. The check accepts
every one and names the same tokens at the same offsets. 26 of the parser's tokens are in no level of the ROM; their
lengths come from the code alone. The check's fuzz target shows that it never reads outside the file.

What it does not check: the other values (points 4 to 11 below, and the rest of the table of ranges), and whether
the level's objects are in the banks of the level whose place it takes. The numbered list below still holds for a file that passes. `g64pack`
prints the check's result for a pack's maker. The game does not refuse a pack on it yet.

## What the game itself guards

The check above runs before a level loads. The game's parser also tests some values where it uses them, so a rule
the check lacks does less harm:

- An object's lookup that finds nothing, or an object without a mesh, no longer gives the parser a null pointer:
  PLATFORM, the three bridges, LANDSCAPE, BACKGROUND, CAMEO_OBJ and WATER. The same for a backdrop's texture and for a
  cameo camera's part, and a cameo has at most 10 extra spheres.
- Before each token the parser asks whether the thing the token's handler uses was made by this level's file
  (`TokenAllowed` in parse.c): a platform, its orbit, its spin, its path's points, its crumble or destructible part, a
  puzzle, an enemy, a vent, a landscape piece and its object, a group of garibs, a cameo, a buzzer. A vent and a
  buzzer keep to their 6 patterns. The parser's own pointers to those things stay as the game left them: they are in
  the game's memory, which `make regress` compares with the baseline.
- An ENEMY must be of one of the game's 59 kinds, the level holds 30 enemies, and an enemy whose object is not in
  the level's banks is not made. A pick-up must be one of the game's 15 and have its object, both from a PICKUP
  token and from a puzzle's action while the level runs.
- Before each token the parser also tests the token's first values (`ValuesAllowed`): an axis is 0, 1 or 2; a
  bridge has 1 to 100 planks and the level 10 rope bridges; a garib's type, a sound's number, an enemy
  instruction's and a cameo instruction's type are ones the game has; an enemy keeps to its 30, 10 and 30
  instructions and a cameo to its 20; a STARTPOINT names a point the path has.
- A platform cannot be its own parent. A TAKESPIN under a parent whose spin has no axis stops the level: the game
  wrote three numbers into a list of two. A vent whose patterns all have 0 frames on stays off; the game called
  itself for it without an end.
- A DESTRUCTIBLE platform's pieces must be of an object the level's banks hold (or of none, as in the game's own
  levels), and a platform breaks into at most 64.
- A level file may not fill the game's memory. The game stops the program when its heap or its list of 1,280 memory
  blocks is full. Before each token the parser asks for 4 MB of free heap and for fewer than 1,100 blocks in use; the
  largest of the game's levels uses 957 blocks and about 1 MB of 22 while it is read.
- The collision planes after the parse must fit too (`G64PlanesFit`, parse.c). Each LANDSCAPE, BACKGROUND or WATER
  actor takes one more block and 120 bytes for each face of its mesh. Before any plane is made, the parser adds them
  up. The level stops as unplayable ("TOO LARGE") when they would leave less than 4 MB free, or would bring the
  blocks in use to 1,100. A level file that named one object some hundreds of times filled the list of blocks here,
  and the game stopped the program. With 16 players the game's own levels need at most 101 blocks and 288,048
  bytes, and reach at most 1,035 blocks in use.
- A landscape actor with collision compares each face of its mesh with every later face (`CheckForInnerEdges`).
  The parser adds up the squares of those face counts, and the level stops ("COLLISION") above 40,000,000. The game's
  own levels reach 340,884 (level 43). Seventeen flat meshes of 1500 faces fit.
- A CAMEO_CAM of a type below 0 takes one of the 10 extra spheres, as every type but 0 and 1 does.
- An ORBITBRIDGE's axis is 0 to 2 or 0x58 to 0x5A. An orbit whose axis was never set moved a platform through a
  list of two numbers with three; the list has three now.
- A WATER whose object has no faces stops the level: the game freed the object and kept a pointer to it.
- A platform's TERRAIN comes before the tokens that free its faces (NOCOLLISION, SPIKE, an EXITDOOR that is not
  shown).
- A FOG whose near end equals its far end stops the level, and a puzzle's action cannot set it so: the game divides
  by the difference when it draws.
- A BACKDROP's two scales are above 0, and its parent is a backdrop the level has.
- A wind makes at most 1,000 particles a frame. A hub's exit asks the list of finished worlds only for the exit
  types 5 to 11. A puzzle's action that sets the level of a water no tag names does nothing. A conveyor on a
  DUMMY slides no texture.
- A TIP's number is 0 to 32. An animation's number below 0 plays the object's first.
- A cameo starts only when every one of its instructions names things that are there (`CameoUsable` in cameo.c): an
  enemy of the level by its number, another instruction of the cameo as its trigger, one of an actor's 8 spheres.
  A puzzle's action can start only a cameo the level has.
- A puzzle condition's and action's number must be one the game has, and their puzzle variables among the level's
  100: the parser tests them as it reads them.
- While the level runs, a condition or an action whose tag meets a platform without the part it needs (a path, a
  spin, an orbit, a confine) is not met, or does nothing for that platform. The same for a spin or an orbit without
  a pause, whose count of pauses the game divided by. An action with a tag that no platform has does nothing.
- An enemy's list of instructions has an end (`NMEGetNewInstruction`): an enemy without instructions, a puzzle's
  action that names an instruction the enemy does not have, and a list whose last instruction's conditions are not
  met leave the enemy with the instruction it has.
- The level is then marked as one that cannot be played (`G64Unplayable`), which is the game's own state for a
  level without its file. The game shows its error screen and runs none of the level's logic.
- Online, the players of such a level go back to the hub, and each is told why. A hub level that cannot be played
  keeps its players.
- `pack_unplayable` in tests/rom/instances.c plays a level whose first platform names a missing object, and one
  whose garibs come before any group, one with an enemy of a kind the level's banks do not hold, one whose broken
  platform leaves pieces of a missing object, and one with 1,300 more platforms than the game's: both players end in
  the hub each time, and no machine disagrees.

POS and the tokens that free a platform's faces are safe on a DUMMY as the game is: the first returns before it
uses the object, and the game's free does nothing for a list that is not there. A head start runs at most 32,767
updates.

The game's code for the hubs, the boss levels, the bonus levels and the scenes expects fixed things in those
levels: platforms by tag, enemies by order, a water by tag. `make swaps` (tests/rom/swaps.c) plays every level with
each other level's file in its place, with two players at random, and the game must play it or stop it as
unplayable. It found these, now guarded: the crystal cave's platforms by tag (InitMorePersonal), the third space
boss's platforms by tag (DoSpaceBoss3), a restart gate without motion (CheckRestartGate, RestartPlayer), and the
error screen's overlays, which wrote their vertices past their buffer (GraphicsStep). A pack that comes from a host
may therefore take the place of any level.

`make bankwalk` found one more. The intro and the ending read their first actor's animation (DoIntroEffects,
DoOutroEffects). A pack's file can put an actor without one first, such as a piece of scenery. Both scenes now stop
as unplayable then.

Not guarded in the game yet: the short forms, which the check before loading refuses; memory that fills after the
level is read, while it runs (the program then stops); and what the last section says was not read.

## How the parser reads

- The file is: a size (4 bytes), a name that ends with a zero byte, then 16-bit tokens with their fields, until
  token 0x7D00 (P:158-162).
- No read is bounded. A missing end token, a cut field or a name without its zero byte reads past the file's
  memory.
- A token the parser does not know is skipped two bytes at a time, without a word (P:174-288). A check must refuse
  it.
- The token values are Glovebox's opcodes (`Glovebox/crates/core/data/level_format.json`): all 110 agree. The byte
  counts differ in three places: ORBITFLIP reads 6 bytes (P:1607), SINEBRIDGE reads 60 (P:1147-1160), and an enemy
  instruction of an unknown type reads only its type and time (`nmeprocess.c:78-105`).
- The tokens SPIKE, CAMEO_CAM and ANIMATE_PLATFORM have handlers that do not report that they took the token. No
  other handler takes it after them, so their lengths hold.
- Some tokens read fewer bytes depending on what came before, and the rest is then read as tokens: BALL_START (by
  the save), the puzzle tokens without a puzzle, TELEPORT whose target tag is not found, ORBIT after two path
  points, ORBITPAUSE and ORBITFLIP after each other, SPINFLIP after SPINPAUSE, SCALE after a POINT, and enemy and
  cameo instructions of unknown types.
- `platform` is a local that is not set before the first PLATFORM or DUMMY (P:105). `lastVent`, `lastLandscape`,
  `lastCameo`, `parsePuzzle`, `parseCondition`, `parseAction`, `parseGaribGroup` and `buzzer` are never reset
  between levels: after the first level they point into the last level's freed memory. A token that uses one of
  them must come after the token that sets it, in the same file.

## The most dangerous

1. **An axis out of range.** SPIN and PENDULUM write four bytes of the file at an offset the file gives (P:1691,
   P:1793). SEESAW, TOPPLE, ORBIT, ROPEBRIDGE and puzzle action 6 index small arrays the same way. An axis must be
   0, 1 or 2.
2. **Puzzle variables.** Actions 11 to 13 write `puzzleVal[tag]` with a tag from the file (`puzzle.c:650-669`). The
   array has 100 entries.
3. **Counts with no limit.** Enemies (30), instructions of one enemy (30, 10 and 30), patterns of one vent or
   buzzer (6), instructions of one cameo (20), rope bridges (10), planks of a rope bridge (1 to 100), a cameo's
   extra camera spheres (10).
4. **Ids that the banks do not hold.** PLATFORM, the bridges, WATER, BACKDROP, ENEMY, CAMEO_CAM and puzzle action
   40 use what the lookup gives at once, and it gives nothing for an unknown id. A platform's object must also
   have a mesh.
5. **Tokens before their owner**, as in the last point of the section above.
6. **A condition's id outside 9 to 44** is cut to a byte and becomes a camera condition with no box
   (`puzzle.c:1169`). A condition or action whose tag meets a platform without the part it needs (a path, a
   motion, an orbit, a confine, a fan, a teleport, a catapult) uses a null pointer. Tag 0 meets every platform
   without a tag.
7. **Division by zero:** a pause type of 0 in conditions 1, 2, 4 and 5; a path with no points in action 3; enemy
   conditions 8, 9 and 10 with a zero value.
8. **Loops without end:** a platform that names itself as its parent (`platform.c:621`); a vent whose patterns all
   have 0 frames on; an enemy's script that runs off the end of its list (`nmeprocess.c:145-150`); a tip whose
   number is past the game's table (`tip.c`).
9. **Stack writes:** ORBIT with no axis set, and TAKESPIN under a parent whose motion has no axis, write three
   numbers into two (P:1585, P:1045). A rope bridge with more than 100 planks overflows a stack array
   (`platform.c:1459`).
10. **Indexes from the file:** a cameo instruction's target, trigger, duration and arguments index the enemies, the
    cameo's own items and an actor's 8 spheres (`cameo.c`); STARTPOINT indexes the path's points (P:1914); action
    22 indexes an enemy's instruction list; an animation number must be below the object's count (`anim.c:39`).
11. **Work without end:** SINEBRIDGE and ORBITBRIDGE run a platform's update count times step times (P:1178,
    P:1240); HEADSTART runs it a given number of times; a destructible platform's debris count; any number of
    objects until the heap is full.

## Value ranges

| Value | Range | Where it is used |
| --- | --- | --- |
| Sound number (AMBIENT_SOUND, SOUND, the platform sound tokens, action 39) | 0 to 419 | `mus-start.c:274` |
| Garib type | 0 to 3 | `collect.c:143, 195` |
| Pick-up type (PICKUP, action 14) | 0 to 14 | `collect.c:887` |
| Enemy type | 0 to 58, and one of Glovebox's 52 real types | `enemy.c:157-194` |
| Enemy instruction type | 0 to 24 | `nmeprocess.c:74-105` |
| Enemy condition | 0 to 22 | `nmeprocess.c:465` on |
| Cameo instruction type | 0 to 6 | P:444-478 |
| Power-up index (condition 35) | 0 to 14 | `puzzle.c:1090` |
| Puzzle variable (conditions 18 to 21, actions 11 to 13) | 0 to 99 | `puzzle.c:1133, 650` |
| Confine side (conditions 22 and 23) | 0 to 5 | `puzzle.c:180-204` |
| Exit type in the hub | 5 to 11, 12 or 21 | `game.c:2577` |
| A platform's own tag | below 10000: rope bridges give their planks tags from 10000 up | P:1131 |
| An object's name | a zero byte within its 8 bytes | `banks.c:800` |

## What depends on the level or the session

BALL_START's length (the save); the tip's number on the easy difficulty; a cameo object's loop by level; the
backdrop's color in the hub; a checkpoint's place by level, last world and difficulty (P:1429-1480); the exit door's
checks by world; and which ids the loaded banks hold. A check needs the borrowed level's number and that level's
bank contents from the player's own ROM.

## Not read

Wind, water and light after the parser; the bullets a vent of an unknown type makes; `RecalculatePlatformInfo`;
`CheckForExit` beyond the lines above; most of `nmeprocess2.c`; and whether the game's "level unplayable" state
(mode 13), which a missing object sets, is safe for the rest of the game.
