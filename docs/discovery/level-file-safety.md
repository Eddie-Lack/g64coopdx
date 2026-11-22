# What a level file must not do

The game's level parser (`weemitt/src/game/parse.c`) is the original game's. It trusts the level file. This file
lists what a level file must satisfy for that parser, and the code that uses what it parsed, to stay inside its
memory. It comes from a read of the code on 22 November 2026; nothing here was tested with a hostile file. Line
numbers are of that day. `P:` means `weemitt/src/game/parse.c`.

It exists for one decision: whether g64coopdx may load a level pack that comes from another player
([level-pack.md](level-pack.md)). Today a pack comes only from a folder the player chose.

## The short answer

A check that makes a hostile level file safe must walk the file exactly as the parser does, hold about a hundred
rules on values and order, and know which objects, sub-objects, meshes and animations the borrowed level's banks
hold in the player's own ROM. Parts of the game that use parsed values were not read (the last section). Until such
a check exists and has been fuzzed against the real parser, a level file from a stranger can crash the game or
write outside its memory.

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
